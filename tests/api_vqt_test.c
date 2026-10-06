#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/vm.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "vqt_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"VQT failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static const char* api_names[]={"vqt","vqts","vqtr","vqtrs","vqtw","vqtsw","vqtrw","vqtrsw"};
static const char* languages[]={"lua","js","moon","yue","fennel","scheme","squirrel","python","wren","janet","wasm","ruby","miniscript","forth"};
static const unsigned fractional[]={1,1,1,1,1,0,1,0,1,0,0,1,1,0};
static unsigned errors;
static int expecting_error;
static char error_text[1024];
static unsigned index_of(const char* value,const char** choices,unsigned size)
{ for(unsigned i=0;i<size;++i) if(!strcmp(value,choices[i])) return i; CHECK(0); return 0; }
static const tic_script* language(const char* name)
{ FOREACH_LANG(s) if(!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
static u64 counter(void* data) { (void)data; return 1; }
static u64 frequency(void* data) { (void)data; return 60; }
static void error(const char* text)
{ fprintf(stderr,expecting_error?"VQT_INVALID_DIAGNOSTIC: %s\n":"VQT runtime error: %s\n",text); snprintf(error_text,sizeof error_text,"%s",text); ++errors; }

/* Wrappers observe actual interpreter routing/conversion and worker IPC.
 * Per-machine PMEM records calls; independent interleaved VMs must not leak.
 * Capture is disabled by default, so real DSP correctness is a separate oracle. */
static int is_raw(unsigned api) { return api==2||api==3||api==6||api==7; }
static double result(unsigned api,unsigned frame)
{
#ifdef TM_VQT_ZERO_BACKEND
    (void)api; (void)frame; return 0;
#else
    static const double raw[]={40000,32768,32769};
    return is_raw(api)&&frame>0?raw[frame-1]:((api+1)*128+frame*16)/1024.0;
#endif
}
static double dispatch(tic_mem* tic,s32 bin,unsigned api,double (*backend)(tic_mem*,s32))
{
    CHECK(!tic_api_pmem(tic,63,0,false));
    unsigned frame=tic_api_pmem(tic,31,0,false);
    tic_api_pmem(tic,1,tic_api_pmem(tic,1,0,false)+1,true);
    tic_api_pmem(tic,2,api,true); tic_api_pmem(tic,3,(u32)bin,true);
    tic_api_pmem(tic,4,0x56515400u+api,true);
#ifdef TM_VQT_ZERO_BACKEND
    double value=backend(tic,bin); CHECK(value==0); return value;
#else
    (void)backend; return result(api,frame);
#endif
}
#define WRAP(name,id) double __real_tic_api_##name(tic_mem*,s32); \
    double __wrap_tic_api_##name(tic_mem* tic,s32 bin) { return dispatch(tic,bin,id,__real_tic_api_##name); }
WRAP(vqt,0) WRAP(vqts,1) WRAP(vqtr,2) WRAP(vqtrs,3)
WRAP(vqtw,4) WRAP(vqtsw,5) WRAP(vqtrw,6) WRAP(vqtrsw,7)
#undef WRAP
static const s32 arguments[]={0,119,57,-1,120,INT32_MIN,INT32_MAX,5};
static void check_result(tic80* tic,const char* id,unsigned api,s32 bin,unsigned frame,int forth)
{
    static const u32 raw_forth[]={2147483647u,2147450880u,2147483647u};
    u32 value=forth?(is_raw(api)&&frame>0?raw_forth[frame-1]:(u32)(s32)(result(api,frame)*65535)):(u32)(result(api,frame)*1024);
#ifdef TM_VQT_ZERO_BACKEND
    value=0;
#endif
    const u32 expected[]={value,frame+1,api,(u32)bin,0x56515400u+api};
    for(unsigned slot=0;slot<5;++slot) {
        u32 got=tic_api_pmem((tic_mem*)tic,slot,0,false);
        if(got!=expected[slot]) fprintf(stderr,"VQT_STATE case=%s frame=%u slot=%u got=%u expected=%u\n",id,frame,slot,got,expected[slot]);
        CHECK(got==expected[slot]);
    }
    CHECK(tic_api_pmem((tic_mem*)tic,31,0,false)==frame+1);
    CHECK(tic->samples.count==1600);
    for(unsigned i=0;i<1600;++i) CHECK(tic->samples.buffer[i]==0);
}
int main(int argc,char** argv)
{
    if(argc>1&&!strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1||(argc==3&&!strcmp(argv[1],"--case")));
#ifdef TM_VQT_ZERO_BACKEND
    CHECK(!tm_fft_supported());
#endif
    CHECK(!setvbuf(stdout,NULL,_IOLBF,0)); unsigned cases=0,seen[14][8][8]={0};
    for(unsigned c=0;c<sizeof vqt_cases/sizeof *vqt_cases;++c) {
        const char* name=vqt_cases[c].language; char id[80];
        snprintf(id,sizeof id,"%s-%s-%s",name,vqt_cases[c].api,vqt_cases[c].name);
        if(argc==3&&strcmp(id,argv[2])) continue;
        unsigned api=index_of(vqt_cases[c].api,api_names,8),lang=index_of(name,languages,14),scenario=vqt_cases[c].scenario;
        CHECK(scenario<8&&(!scenario||scenario<7||fractional[lang]));
        CHECK(!seen[lang][api][scenario]++);
        tic_cartridge* cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
        strcpy(cart->code.data,vqt_cases[c].source);
        if(vqt_cases[c].binary) { memcpy(cart->binary.data,vqt_cases[c].binary,vqt_cases[c].size); cart->binary.size=vqt_cases[c].size; }
        u8* bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        tic80* direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct);
        direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm* vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        cart->lang=language("lua")->id; cart->binary.size=0;
        strcpy(cart->code.data,"function TIC() pmem(0,vqtrsw(43)*1024) pmem(31,pmem(31)+1) end\n");
        bytes=malloc(sizeof *cart*2); CHECK(bytes); size=tic_cart_save(cart,bytes); CHECK(size>0);
        tm_vm* peer=NULL; CHECK(tm_vm_open(&peer,bytes,size)==TM_VM_OK); free(bytes); free(cart);
        printf("VQT_START case=%s\n",id);
        for(unsigned frame=0;frame<4;++frame) {
            CHECK(tm_vm_tick(peer,(tic80_input){0},0)==TM_VM_OK);
            check_result(tm_vm_product(peer),"peer",7,43,frame,0);
            tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(!errors);
            check_result(direct,id,api,arguments[scenario],frame,!strcmp(name,"forth"));
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
            check_result(tm_vm_product(vm),id,api,arguments[scenario],frame,!strcmp(name,"forth"));
        }
        tm_vm_close(peer); tm_vm_close(vm); tic80_delete(direct);
        CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD); ++cases;
        printf("VQT_PASS case=%s frames=4 exact_dispatch=1 exact_arguments=1 exact_result=1 peer_unaffected=1\n",id);
    }
    unsigned invalid=0,invalid_seen[2][8][5]={0};
    const char* bad_mini[]={"missing","null","string","high","low"};
    const char* bad_wasm[]={"f32","arity","argument"};
    for(unsigned c=0;c<sizeof vqt_invalid_cases/sizeof *vqt_invalid_cases;++c) {
        const char* name=vqt_invalid_cases[c].language; char id[80];
        snprintf(id,sizeof id,"%s-%s-%s",name,vqt_invalid_cases[c].api,vqt_invalid_cases[c].name);
        if(argc==3&&strcmp(id,argv[2])) continue;
        unsigned api=index_of(vqt_invalid_cases[c].api,api_names,8),wasm=!strcmp(name,"wasm");
        CHECK(wasm||!strcmp(name,"miniscript"));
        unsigned scenario=index_of(vqt_invalid_cases[c].name,wasm?bad_wasm:bad_mini,wasm?3:5);
        CHECK(!invalid_seen[wasm][api][scenario]++);
        tic_cartridge* cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
        strcpy(cart->code.data,vqt_invalid_cases[c].source);
        if(vqt_invalid_cases[c].binary) { memcpy(cart->binary.data,vqt_invalid_cases[c].binary,vqt_invalid_cases[c].size); cart->binary.size=vqt_invalid_cases[c].size; }
        u8* bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        tic80* direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct); direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm* vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        errors=0; error_text[0]=0; expecting_error=1;
        tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct);
        expecting_error=0; CHECK(errors>0); CHECK(strstr(error_text,wasm?"function signature mismatch":"invalid "));
        CHECK(tic_api_pmem((tic_mem*)direct,1,0,false)==0);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_ERROR);
        CHECK(tic_api_pmem((tic_mem*)tm_vm_product(vm),1,0,false)==0);
        tm_vm_close(vm); tic80_delete(direct);
        memset(cart,0,sizeof *cart); cart->lang=language("lua")->id;
        strcpy(cart->code.data,"function TIC() pmem(30,pmem(30)+1) end\n");
        bytes=malloc(sizeof *cart*2); CHECK(bytes); size=tic_cart_save(cart,bytes); CHECK(size>0); free(cart);
        CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        for(unsigned frame=0;frame<2;++frame) { CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); CHECK(tic_api_pmem((tic_mem*)tm_vm_product(vm),30,0,false)==frame+1); }
        tm_vm_close(vm); CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD); errors=0; ++invalid;
        printf("VQT_INVALID_PASS case=%s no_engine_call=1 direct_worker_error=1 recovery=1\n",id);
    }
    if(argc==1) {
        CHECK(cases==856&&invalid==64);
        for(unsigned lang=0;lang<14;++lang) for(unsigned api=0;api<8;++api) for(unsigned s=0;s<8;++s)
            CHECK(seen[lang][api][s]==(s<7||fractional[lang]));
        for(unsigned wasm=0;wasm<2;++wasm) for(unsigned api=0;api<8;++api) for(unsigned s=0;s<5;++s)
            CHECK(invalid_seen[wasm][api][s]==(s<(wasm?3:5)));
    } else CHECK(cases+invalid==1);
    printf("VQT binding contract: %u cases; fourteen runtimes; eight APIs; direct/worker dispatch, arguments, results and peer isolation passed\n",cases);
    printf("VQT invalid binding contract: %u cases; MiniScript bin guards, WASM f64/one-i32 signatures, no engine calls and worker recovery passed\n",invalid);
#ifdef TM_VQT_ZERO_BACKEND
    puts("VQT capture-disabled contract: every valid direct/worker call reached the real unsupported backend and returned zero");
#endif
    return 0;
}
