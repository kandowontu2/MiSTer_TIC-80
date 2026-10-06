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
#include "fft_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FFT failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned errors;
static int expecting_error;
static char error_text[1024];
static const tic_script *language(const char *name)
{ FOREACH_LANG(s) if(!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
static u64 counter(void *data) { (void)data; return 1; }
static u64 frequency(void *data) { (void)data; return 60; }
static void error(const char *text) { fprintf(stderr,expecting_error?"FFT_INVALID_DIAGNOSTIC: %s\n":"FFT runtime error: %s\n",text); snprintf(error_text,sizeof error_text,"%s",text); ++errors; }

/* Link wrappers distinguish routing despite the disabled capture backend.
 * Arguments/results cross each real interpreter and the actual worker IPC.
 * Calls are recorded in the calling machine's RAM, so interleaved machines
 * must retain independent results. No shared test state enters a worker. */
static double result(unsigned api,unsigned frame)
{
    /* Raw magnitudes can exceed the normalized 0..1 range. Exercise the
     * signed-32-bit Forth scaled result just below and above saturation. */
    static const double raw[]={40000.0,32768.0,32769.0};
    return api>=2 && frame>0?raw[frame-1]:((api+1)*128+frame*16)/1024.0;
}
static double dispatch(tic_mem *tic,s32 start,s32 end,unsigned api)
{
    CHECK(!tic_api_pmem(tic,63,0,false));
    unsigned frame=tic_api_pmem(tic,31,0,false);
    tic_api_pmem(tic,1,tic_api_pmem(tic,1,0,false)+1,true);
    tic_api_pmem(tic,2,api,true);
    tic_api_pmem(tic,3,(u32)start,true); tic_api_pmem(tic,4,(u32)end,true);
    return result(api,frame);
}
double __wrap_tic_api_fft(tic_mem *tic,s32 start,s32 end) { return dispatch(tic,start,end,0); }
double __wrap_tic_api_ffts(tic_mem *tic,s32 start,s32 end) { return dispatch(tic,start,end,1); }
double __wrap_tic_api_fftr(tic_mem *tic,s32 start,s32 end) { return dispatch(tic,start,end,2); }
double __wrap_tic_api_fftrs(tic_mem *tic,s32 start,s32 end) { return dispatch(tic,start,end,3); }

/* Independent of generated script calls and the core's capture implementation. */
static const s32 arguments[][2]={{0,-1},{1023,-1},{3,19},{-7,23},{19,3},{0,1023},{1024,2048}};
static void check_result(tic80 *tic,const char *id,unsigned api,unsigned scenario,unsigned frame,int forth)
{
    /* Expectations for saturation are independent of the staged word body. */
    static const u32 raw_forth[]={2147483647u,2147450880u,2147483647u};
    u32 value=forth?(api>=2 && frame>0?raw_forth[frame-1]:(u32)(s32)(result(api,frame)*65535.0)):(u32)(result(api,frame)*1024.0);
    const u32 expected[]={value,
        frame+1,api,(u32)arguments[scenario][0],(u32)arguments[scenario][1]};
    for(unsigned slot=0;slot<5;++slot) {
        u32 got=tic_api_pmem((tic_mem*)tic,slot,0,false);
        if(got!=expected[slot]) fprintf(stderr,"FFT_STATE case=%s frame=%u slot=%u got=%u expected=%u\n",id,frame,slot,got,expected[slot]);
        CHECK(got==expected[slot]);
    }
    CHECK(tic_api_pmem((tic_mem*)tic,31,0,false)==frame+1);
    CHECK(tic->samples.count==1600);
    for(unsigned i=0;i<1600;++i) CHECK(tic->samples.buffer[i]==0);
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1 || (argc==3 && !strcmp(argv[1],"--case")));
    CHECK(!setvbuf(stdout,NULL,_IOLBF,0)); unsigned cases=0;
    for(unsigned c=0;c<sizeof fft_cases/sizeof *fft_cases;++c) {
        const char *name=fft_cases[c].language,*api_name=fft_cases[c].api;
        char id[80]; snprintf(id,sizeof id,"%s-%s-%s",name,api_name,fft_cases[c].name);
        if(argc==3 && strcmp(id,argv[2])) continue;
        unsigned api=!strcmp(api_name,"fft")?0:!strcmp(api_name,"ffts")?1:!strcmp(api_name,"fftr")?2:3;
        CHECK(!strcmp(api_name,"fft") || !strcmp(api_name,"ffts") || !strcmp(api_name,"fftr") || !strcmp(api_name,"fftrs"));
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
        strcpy(cart->code.data,fft_cases[c].source);
        if(fft_cases[c].binary) { memcpy(cart->binary.data,fft_cases[c].binary,fft_cases[c].size); cart->binary.size=fft_cases[c].size; }
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        tic80 *direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct);
        direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        /* Exercise a different FFT dispatch while this machine is active. */
        cart->lang=language("lua")->id; cart->binary.size=0;
        strcpy(cart->code.data,"function TIC() pmem(0,fftrs(43,71)*1024) pmem(31,pmem(31)+1) end\n");
        bytes=malloc(sizeof *cart*2); CHECK(bytes); size=tic_cart_save(cart,bytes); CHECK(size>0);
        tm_vm *peer=NULL; CHECK(tm_vm_open(&peer,bytes,size)==TM_VM_OK); free(bytes); free(cart);
        printf("FFT_START case=%s\n",id);
        for(unsigned frame=0;frame<4;++frame) {
            CHECK(tm_vm_tick(peer,(tic80_input){0},0)==TM_VM_OK);
            tic80 *p=tm_vm_product(peer);
            CHECK(tic_api_pmem((tic_mem*)p,0,0,false)==(u32)(result(3,frame)*1024.0));
            CHECK(tic_api_pmem((tic_mem*)p,1,0,false)==frame+1);
            CHECK(tic_api_pmem((tic_mem*)p,2,0,false)==3);
            CHECK(tic_api_pmem((tic_mem*)p,3,0,false)==43 && tic_api_pmem((tic_mem*)p,4,0,false)==71);
            CHECK(tic_api_pmem((tic_mem*)p,31,0,false)==frame+1);
            tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(!errors);
            check_result(direct,id,api,fft_cases[c].scenario,frame,!strcmp(name,"forth"));
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
            check_result(tm_vm_product(vm),id,api,fft_cases[c].scenario,frame,!strcmp(name,"forth"));
        }
        tm_vm_close(peer); tm_vm_close(vm); tic80_delete(direct);
        CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD); ++cases;
        printf("FFT_PASS case=%s frames=4 exact_dispatch=1 exact_arguments=1 exact_result=1 peer_unaffected=1\n",id);
    }
    unsigned invalid=0;
    for(unsigned c=0;c<sizeof fft_invalid_cases/sizeof *fft_invalid_cases;++c) {
        const char *name=fft_invalid_cases[c].language;
        char id[80]; snprintf(id,sizeof id,"%s-%s-%s",name,fft_invalid_cases[c].api,fft_invalid_cases[c].name);
        if(argc==3 && strcmp(id,argv[2])) continue;
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
        strcpy(cart->code.data,fft_invalid_cases[c].source);
        if(fft_invalid_cases[c].binary) { memcpy(cart->binary.data,fft_invalid_cases[c].binary,fft_invalid_cases[c].size); cart->binary.size=fft_invalid_cases[c].size; }
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        tic80 *direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct); direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        errors=0; error_text[0]=0; expecting_error=1;
        tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct);
        expecting_error=0; CHECK(errors>0);
        CHECK(strstr(error_text,!strcmp(name,"wasm")?"function signature mismatch":"invalid "));
        CHECK(tic_api_pmem((tic_mem*)direct,1,0,false)==0);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_ERROR);
        CHECK(tic_api_pmem((tic_mem*)tm_vm_product(vm),1,0,false)==0);
        tm_vm_close(vm); tic80_delete(direct);
        memset(cart,0,sizeof *cart); cart->lang=language("lua")->id;
        strcpy(cart->code.data,"function TIC() pmem(30,pmem(30)+1) end\n");
        bytes=malloc(sizeof *cart*2); CHECK(bytes); size=tic_cart_save(cart,bytes); CHECK(size>0); free(cart);
        CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        for(unsigned frame=0;frame<2;++frame) { CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); CHECK(tic_api_pmem((tic_mem*)tm_vm_product(vm),30,0,false)==frame+1); }
        tm_vm_close(vm); CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD); errors=0; ++invalid;
        printf("FFT_INVALID_PASS case=%s no_engine_call=1 direct_worker_error=1 recovery=1\n",id);
    }
    CHECK(argc==1?(cases==392 && invalid==40):(cases+invalid==1));
    printf("FFT binding contract: %u cases; fourteen runtimes; direct/worker dispatch, arguments, result and peer isolation passed\n",cases);
    printf("FFT invalid binding contract: %u cases; MiniScript argument guards, WASM f64/two-i32 signatures, no engine calls and worker recovery passed\n",invalid);
    return 0;
}
