#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/vm.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "spectrum_args_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"Spectrum args failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static const char* languages[]={"lua","moon","yue","fennel","squirrel","wren","js"};
static const char* apis[]={"vqt","vqts","vqtr","vqtrs","vqtw","vqtsw","vqtrw","vqtrsw","fft","ffts","fftr","fftrs"};
static const char* valid_names[]={"integer","fraction","negative-fraction","maximum","minimum","upper-fraction","lower-fraction","null","true","numeric-string","bad-string","nan","inf","negative-inf","wrap-high","wrap-low","extra","missing","end-null","end-fraction","end-undefined"};
static const char* invalid_names[]={"nan","inf","negative-inf","high","low","huge","negative-huge","wrong-null","wrong-true","wrong-numeric-string","wrong-list","missing","throw","symbol","end-nan","end-high","end-inf","end-throw","end-symbol","extra","end-wrong-type"};
static unsigned errors; static char error_text[2048];
static void error(const char* text) { ++errors; size_t used=strlen(error_text); snprintf(error_text+used,sizeof error_text-used,"%s\n",text); fprintf(stderr,"SPECTRUM_ARGS_ERROR: %s\n",text); }
static unsigned index_of(const char* value,const char** choices,unsigned count)
{ for(unsigned i=0;i<count;++i) if(!strcmp(value,choices[i])) return i; CHECK(0); return 0; }
static const tic_script* language(const char* name)
{ FOREACH_LANG(s) if(!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
static u64 counter(void* ignored) { (void)ignored; return 1; }
static u64 frequency(void* ignored) { (void)ignored; return 60; }
static double result(unsigned api,unsigned frame) { return ((api+1)*64+frame*8)/1024.0; }
static double dispatch(tic_mem* tic,s32 first,s32 last,unsigned api)
{
    CHECK(!tic_api_pmem(tic,63,0,false));
    unsigned frame=tic_api_pmem(tic,31,0,false);
    tic_api_pmem(tic,1,tic_api_pmem(tic,1,0,false)+1,true); tic_api_pmem(tic,2,api,true);
    tic_api_pmem(tic,3,(u32)first,true); tic_api_pmem(tic,4,(u32)last,true); tic_api_pmem(tic,5,0x41524700u+api,true);
    return result(api,frame);
}
#define VQT(name,id) double __wrap_tic_api_##name(tic_mem* tic,s32 bin) { return dispatch(tic,bin,0,id); }
VQT(vqt,0) VQT(vqts,1) VQT(vqtr,2) VQT(vqtrs,3) VQT(vqtw,4) VQT(vqtsw,5) VQT(vqtrw,6) VQT(vqtrsw,7)
#define FFT(name,id) double __wrap_tic_api_##name(tic_mem* tic,s32 first,s32 last) { return dispatch(tic,first,last,id); }
FFT(fft,8) FFT(ffts,9) FFT(fftr,10) FFT(fftrs,11)
static int valid_enabled(unsigned lang,unsigned api,unsigned scenario)
{
    if(scenario<7) return 1;
    if(scenario<11) return lang!=5;
    if(scenario<14) return lang==6;
    if(scenario<16) return lang==4||lang==6;
    if(scenario==16) return lang!=5;
    if(scenario==17) return lang==6;
    if(scenario==18) return api>=8&&lang!=5;
    if(scenario==19) return api>=8;
    return api>=8&&lang==6;
}
static int invalid_enabled(unsigned lang,unsigned api,unsigned scenario)
{
    if(scenario<7) return lang!=6;
    if(scenario<11) return lang==5;
    if(scenario==11) return lang!=6;
    if(scenario<14) return lang==6;
    if(scenario<17) return api>=8&&lang!=6;
    if(scenario<19) return api>=8&&lang==6;
    if(scenario==19) return lang==5;
    return api>=8&&lang==5;
}
static void expected(unsigned lang,unsigned api,unsigned scenario,s32* first,s32* last)
{
    const s32 values[]={5,5,-5,INT32_MAX,INT32_MIN,INT32_MAX,INT32_MIN,0,0,5,0,0,0,0,5,-5,5,0,5,5,5};
    *first=values[scenario]; *last=api>=8?-1:0;
    if(lang==4) { if(scenario==5) *first=2147483520; if(scenario==6) *first=-2147483520; if(scenario==9) *first=0; }
    if((lang==4||lang==6)&&scenario==8) *first=1;
    if(api>=8) { if(scenario==16) *last=9; if(scenario==18) *last=0; if(scenario==19) *last=7; }
}
static void check_product(tic80* tic,unsigned api,s32 first,s32 last,unsigned frame,const char* id)
{
    const u32 values[]={(u32)(result(api,frame)*1024),frame+1,api,(u32)first,(u32)last,0x41524700u+api};
    for(unsigned i=0;i<6;++i) {
        u32 got=tic_api_pmem((tic_mem*)tic,i,0,false);
        if(got!=values[i]) fprintf(stderr,"SPECTRUM_ARGS_STATE case=%s frame=%u slot=%u expected=%u got=%u\n",id,frame,i,values[i],got);
        CHECK(got==values[i]);
    }
    CHECK(tic_api_pmem((tic_mem*)tic,31,0,false)==frame+1&&tic->samples.count==1600);
    for(unsigned i=0;i<1600;++i) CHECK(!tic->samples.buffer[i]);
}
static u8* cartridge(const char* name,const char* code,s32* size)
{
    tic_cartridge* cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
    CHECK(strlen(code)<sizeof cart->code.data); strcpy(cart->code.data,code);
    u8* bytes=malloc(sizeof *cart*2); CHECK(bytes); *size=tic_cart_save(cart,bytes); CHECK(*size>0); free(cart); return bytes;
}
int main(int argc,char** argv)
{
    if(argc>1&&!strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1||(argc==3&&!strcmp(argv[1],"--case"))); CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
    unsigned valid=0,invalid=0,valid_seen[7][12][21]={0},invalid_seen[7][12][21]={0};
    for(unsigned c=0;c<sizeof spectrum_valid_cases/sizeof *spectrum_valid_cases;++c) {
        const char* name=spectrum_valid_cases[c].language; char id[100]; snprintf(id,sizeof id,"%s-%s-%s",name,spectrum_valid_cases[c].api,spectrum_valid_cases[c].name);
        if(argc==3&&strcmp(id,argv[2])) continue;
        unsigned lang=index_of(name,languages,7),api=index_of(spectrum_valid_cases[c].api,apis,12),scenario=index_of(spectrum_valid_cases[c].name,valid_names,21);
        CHECK(valid_enabled(lang,api,scenario)&&!valid_seen[lang][api][scenario]++); s32 first,last; expected(lang,api,scenario,&first,&last);
        s32 size; u8* bytes=cartridge(name,spectrum_valid_cases[c].source,&size);
        tic80* direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct); direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm* vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes); errors=0;
        printf("SPECTRUM_ARGS_START case=%s\n",id);
        for(unsigned frame=0;frame<3;++frame) {
            tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(!errors);
            check_product(direct,api,first,last,frame,id); CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); check_product(tm_vm_product(vm),api,first,last,frame,id);
        }
        tm_vm_close(vm); tic80_delete(direct); CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD); ++valid;
        printf("SPECTRUM_ARGS_PASS case=%s direct_worker=1 exact_coercion=1\n",id);
    }
    for(unsigned c=0;c<sizeof spectrum_invalid_cases/sizeof *spectrum_invalid_cases;++c) {
        const char* name=spectrum_invalid_cases[c].language; char id[100]; snprintf(id,sizeof id,"%s-%s-%s",name,spectrum_invalid_cases[c].api,spectrum_invalid_cases[c].name);
        if(argc==3&&strcmp(id,argv[2])) continue;
        unsigned lang=index_of(name,languages,7),api=index_of(spectrum_invalid_cases[c].api,apis,12),scenario=index_of(spectrum_invalid_cases[c].name,invalid_names,21);
        CHECK(invalid_enabled(lang,api,scenario)&&!invalid_seen[lang][api][scenario]++);
        s32 size; u8* bytes=cartridge(name,spectrum_invalid_cases[c].source,&size);
        tic80* direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct); direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm* vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes); errors=0; error_text[0]=0;
        printf("SPECTRUM_ARGS_INVALID_START case=%s\n",id);
        tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(errors>0);
        if(scenario<11||scenario==14||scenario==15||scenario==16||scenario==20) CHECK(strstr(error_text,"invalid spectrum integer"));
        if(scenario==12||scenario==17) CHECK(strstr(error_text,"bad spectrum value"));
        CHECK(tic_api_pmem((tic_mem*)direct,1,0,false)==0);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_ERROR); CHECK(tic_api_pmem((tic_mem*)tm_vm_product(vm),1,0,false)==0);
        tm_vm_close(vm); tic80_delete(direct);
        bytes=cartridge("lua","function TIC() pmem(0,vqt(5)*1024) pmem(31,pmem(31)+1) end\n",&size);
        CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        for(unsigned frame=0;frame<2;++frame) { CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); check_product(tm_vm_product(vm),0,5,0,frame,"recovery"); }
        tm_vm_close(vm); CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD); errors=0; ++invalid;
        printf("SPECTRUM_ARGS_INVALID_PASS case=%s no_engine_call=1 direct_worker_error=1 recovery=1\n",id);
    }
    if(argc==1) {
        CHECK(valid==1100&&invalid==744);
        for(unsigned lang=0;lang<7;++lang) for(unsigned api=0;api<12;++api) for(unsigned s=0;s<21;++s) {
            CHECK(valid_seen[lang][api][s]==valid_enabled(lang,api,s)); CHECK(invalid_seen[lang][api][s]==invalid_enabled(lang,api,s));
        }
    } else CHECK(valid+invalid==1);
    printf("Spectrum argument contract: valid=%u invalid=%u runtimes=7 APIs=12; finite truncation, legacy coercion/defaults, rejected conversions, no engine call on errors and exec-worker recovery passed\n",valid,invalid);
    return 0;
}
