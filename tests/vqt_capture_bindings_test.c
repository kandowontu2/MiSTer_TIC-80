/* Real callback -> FFT/VQT -> cartridge API -> direct/exec-worker PMEM.
 * Only the capture-device boundary and its deterministic input are doubled. */
#define TM_VQT_SPECTRUM_NO_MAIN
#include "vqt_spectrum_test.c"
#include "tic80_mister/vm.h"
#include "cart.h"
#include "script.h"
#include <errno.h>
#include <sys/wait.h>
#include "vqt_capture_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"VQT capture failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static const char* languages[]={"lua","js","moon","yue","fennel","scheme","squirrel","python","wren","janet","wasm","ruby","miniscript","forth"};
static const char* names[]={"vqt","vqts","vqtr","vqtrs","vqtw","vqtsw","vqtrw","vqtrsw"};
static const unsigned fractional[]={1,1,1,1,1,0,1,0,1,0,0,1,1,0};
static unsigned capture_frame,errors,queries,nonzero;
static void error(const char* text) { ++errors; fprintf(stderr,"VQT capture runtime error: %s\n",text); }
static unsigned index_of(const char* value,const char** choices,unsigned count)
{ for(unsigned i=0;i<count;++i) if(!strcmp(value,choices[i])) return i; CHECK(0); return 0; }
static const tic_script* language(const char* name)
{ FOREACH_LANG(s) if(!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
static u64 counter(void* unused) { (void)unused; return 1; }
static u64 frequency(void* unused) { (void)unused; return 60; }
static void signal_frame(unsigned frame,float* stereo,float* mono)
{
    unsigned random=0xb013cf97; const double tau=2*acos(-1.0);
    for(unsigned i=0;i<frame*VN;++i) { random^=random<<13; random^=random>>17; random^=random<<5; }
    for(int j=0;j<VN;++j) {
        random^=random<<13; random^=random>>17; random^=random<<5;
        double noise=((double)(random&0xffff)/65535-.5)*0.02, value=noise;
        switch(frame) {
        case 0: value=0; break;
        case 1: case 9: value+=0.25*cos(tau*440*j/44100); break;
        case 2: value+=0.0025*cos(tau*440*j/44100); break;
        case 3: value+=0.5*sin(tau*880*j/44100); break;
        case 4: value+=0.2*cos(tau*19.445*j/44100)+0.1*sin(tau*18800*j/44100); break;
        case 5: value+=0.1*cos(tau*147.37*j/44100)+0.2*sin(tau*4000.25*j/44100); break;
        case 6: value+=j==123?0.5:0; break;
        case 7: value+=0.125; break;
        case 8: break;
        default: CHECK(0);
        }
        /* Independent float channels; the callback must mix these to mono. */
        double opposing=0.03125*cos(tau*1600.25*j/44100);
        stereo[2*j]=(float)(value+opposing); stereo[2*j+1]=(float)(value-opposing);
        mono[j]=(float)(((double)stereo[2*j]+stereo[2*j+1])/2);
    }
}
void __real_FFT_GetFFT(float*);
bool __real_FFT_Open(bool,const char*);
bool __wrap_FFT_Open(bool loopback,const char* device)
{ capture_frame=0; return __real_FFT_Open(loopback,device); }
void __wrap_FFT_GetFFT(float* target)
{
    float stereo[VN*2],mono[VN]; CHECK(active_device&&capture_frame<10);
    signal_frame(capture_frame++,stereo,mono); feed(stereo,VN);
    __real_FFT_GetFFT(target);
}
static double expected(unsigned api,int bin)
{
    double values[]={raw[bin]/peak,fmin(1,smooth[bin]/peak),raw[bin],smooth[bin],white[bin]/white_peak,fmin(1,white_smooth[bin]/white_peak),white[bin],white_smooth[bin]};
    return values[api];
}
static void reset_reference(void)
{ memset(raw,0,sizeof raw); memset(smooth,0,sizeof smooth); memset(white,0,sizeof white); memset(white_smooth,0,sizeof white_smooth); peak=white_peak=1; frames=0; }
static void check_product(tic80* tic,unsigned api,unsigned lang,unsigned frame,unsigned script_frame,const char* where)
{
    CHECK(tic_api_pmem((tic_mem*)tic,250,0,false)==script_frame+1);
    CHECK(tic->samples.count==1600);
    for(unsigned i=0;i<1600;++i) CHECK(!tic->samples.buffer[i]);
    for(unsigned slot=0;slot<120+4+fractional[lang];++slot) {
        int bin=slot<120?(int)slot:slot==124?5:-1;
        double scale=lang==13?65535:1024;
        double wanted=bin<0?0:expected(api,bin)*scale;
        CHECK(isfinite(wanted)&&wanted>=0&&wanted<INT32_MAX);
        u32 got=tic_api_pmem((tic_mem*)tic,slot,0,false);
        /* Integer transport adds <1 cell. DSP bounds match the separately
         * qualified oracle, scaled to each language's numeric ABI. */
        double tolerance=1+scale*((api==2||api==3)?0.0003:0.0001)+wanted*0.001;
        if(fabs(got-floor(wanted))>tolerance) {
            fprintf(stderr,"VQT_CAPTURE_STATE language=%s api=%s frame=%u slot=%u path=%s expected=%.17g got=%u tolerance=%.17g\n",languages[lang],names[api],frame,slot,where,wanted,got,tolerance);
            CHECK(0);
        }
        if(bin<0||frame==0) CHECK(got==0);
        nonzero+=got!=0; ++queries;
    }
}
int main(int argc,char** argv)
{
    if(argc>1&&!strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1||(argc==3&&!strcmp(argv[1],"--case")));
    CHECK(!setvbuf(stdout,NULL,_IOLBF,0)); CHECK(tm_fft_supported());
    oracle_self_test(); CHECK(FFT_Open(false,NULL)); check_kernels(); FFT_Close(); clean();
    unsigned cases=0,seen[14][8]={0};
    for(unsigned c=0;c<sizeof vqt_capture_cases/sizeof *vqt_capture_cases;++c) {
        const char* name=vqt_capture_cases[c].language; char id[64];
        snprintf(id,sizeof id,"%s-%s",name,vqt_capture_cases[c].api);
        if(argc==3&&strcmp(id,argv[2])) continue;
        unsigned lang=index_of(name,languages,14),api=index_of(vqt_capture_cases[c].api,names,8); CHECK(!seen[lang][api]++);
        tic_cartridge* cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
        CHECK(strlen(vqt_capture_cases[c].source)<sizeof cart->code.data); strcpy(cart->code.data,vqt_capture_cases[c].source);
        if(vqt_capture_cases[c].binary) { CHECK(vqt_capture_cases[c].size<=sizeof cart->binary.data); memcpy(cart->binary.data,vqt_capture_cases[c].binary,vqt_capture_cases[c].size); cart->binary.size=vqt_capture_cases[c].size; }
        u8* bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0); free(cart);
        tic80* direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct); direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_fft_config config={0}; CHECK(!tm_fft_configure(&config,NULL)); tm_vm* vm=NULL;
        CHECK(tm_vm_open_configured(&vm,bytes,size,&config)==TM_VM_OK); CHECK(tm_vm_fft_status(vm)==TM_FFT_ACTIVE); free(bytes);
        CHECK(FFT_Open(false,NULL)); capture_frame=0;
        reset_reference();
        printf("VQT_CAPTURE_START case=%s\n",id);
        for(unsigned frame=0;frame<10;++frame) {
            float stereo[VN*2],mono[VN]; signal_frame(frame,stereo,mono); update_reference(mono);
            tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(!errors&&capture_frame==frame+1);
            check_product(direct,api,lang,frame,frame,"direct");
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); CHECK(tm_vm_fft_status(vm)==TM_FFT_ACTIVE);
            check_product(tm_vm_product(vm),api,lang,frame,frame,"worker"); ++frames;
        }
        CHECK(tm_vm_fft_pause(vm)==TM_VM_OK&&tm_vm_fft_status(vm)==TM_FFT_PAUSED);
        FFT_Close(); clean(); reset_reference();
        tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(!errors&&capture_frame==10);
        check_product(direct,api,lang,0,10,"direct-paused");
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK&&tm_vm_fft_status(vm)==TM_FFT_PAUSED);
        check_product(tm_vm_product(vm),api,lang,0,10,"worker-paused");
        CHECK(tm_vm_fft_resume(vm)==TM_VM_OK&&tm_vm_fft_status(vm)==TM_FFT_ACTIVE);
        CHECK(FFT_Open(false,NULL)); CHECK(capture_frame==0); reset_reference();
        for(unsigned frame=0;frame<10;++frame) {
            float stereo[VN*2],mono[VN]; signal_frame(frame,stereo,mono); update_reference(mono);
            tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(!errors&&capture_frame==frame+1);
            check_product(direct,api,lang,frame,11+frame,"direct-resumed");
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK&&tm_vm_fft_status(vm)==TM_FFT_ACTIVE);
            check_product(tm_vm_product(vm),api,lang,frame,11+frame,"worker-resumed"); ++frames;
        }
        FFT_Close(); clean(); tic80_delete(direct); tm_vm_close(vm);
        CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD); ++cases;
        printf("VQT_CAPTURE_PASS case=%s frames=21 bins=120 direct_worker=1 real_backend=1 pause_resume=1\n",id);
    }
    if(argc==1) { CHECK(cases==112&&queries==586320&&nonzero>0); for(unsigned lang=0;lang<14;++lang) for(unsigned api=0;api<8;++api) CHECK(seen[lang][api]==1); }
    else CHECK(cases==1);
    printf("VQT capture binding oracle: cases=%u runtimes=14 APIs=8 frames=21 bins=120 queries=%u nonzero=%u; real callback/FFT/VQT, direct/worker numeric results, stereo mixing, invalid bins, pause/resume resets and silent playback passed\n",cases,queries,nonzero);
    return 0;
}
