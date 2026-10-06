#define _GNU_SOURCE
#include "tic80_mister/vm.h"
#include "tic80_mister/studio_session.h"
#include "cart.h"
#include "tools.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static const char* code="-- script: lua\nn=0\nfunction TIC() n=n+1 pmem(0,n) pmem(1,math.floor(fftr(32)+0.5)) pmem(2,math.floor(fftr(64)+0.5)) pmem(3,math.floor(fftrs(32)+0.5)) pmem(4,math.floor(fft(32)*1024+0.5)) pmem(5,math.floor(ffts(32)*1024+0.5)) cls(1) end\n";
static char directory[4096],logfile[4096];
static u8* bytes; static s32 byte_count;
static unsigned descriptors(void)
{
    DIR* dir=opendir("/proc/self/fd"); assert(dir); unsigned count=0; struct dirent* item;
    while((item=readdir(dir))) if(item->d_name[0]!='.') ++count;
    closedir(dir); return count;
}
static u32 value(tm_vm* vm,unsigned at) { return ((tic_mem*)tm_vm_product(vm))->ram->persistent.data[at]; }
static tm_vm* open_vm(const char* name,int expected)
{
    tm_fft_config config={0}; if(name) assert(!tm_fft_configure(&config,name));
    tm_vm* vm=NULL; assert(tm_vm_open_configured(&vm,bytes,byte_count,&config)==TM_VM_OK);
    assert(vm&&tm_vm_fft_status(vm)==expected);
    memset(&config,0xa5,sizeof config); /* caller storage is no longer needed */
    return vm;
}
static void tick(tm_vm* vm)
{
    assert(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); assert(tm_vm_product(vm)->samples.count==1600);
    /* Microphone input must not be mixed into cartridge playback. */
    for(unsigned i=0;i<1600;++i) assert(!tm_vm_product(vm)->samples.buffer[i]);
}
static void vm_cases(void)
{
    unsigned baseline=descriptors();
    tm_vm *a=open_vm("Mic A",TM_FFT_ACTIVE),*b=open_vm("Mic B",TM_FFT_ACTIVE);
    tick(a); tick(b); assert(value(a,1)==512&&value(a,2)==0&&value(a,3)==205&&value(a,4)==1024&&value(a,5)==410);
    assert(value(b,1)==0&&value(b,2)==512);
    tick(a); assert(value(a,0)==2&&value(a,1)==512&&value(a,3)==328&&value(a,4)==1024&&value(a,5)==655);
    tm_vm* disabled=open_vm(NULL,TM_FFT_DISABLED); tick(disabled);
    for(unsigned i=1;i<=5;++i) assert(!value(disabled,i));
    tm_vm_close(disabled);
    tm_vm* busy=open_vm("Mic A",TM_FFT_UNAVAILABLE); tick(busy); assert(!value(busy,1)); tm_vm_close(busy);
    assert(tm_vm_fft_pause(a)==TM_VM_OK&&tm_vm_fft_status(a)==TM_FFT_PAUSED);
    tm_vm* replacement=open_vm("Mic A",TM_FFT_ACTIVE); tick(replacement); assert(value(replacement,1)==512); tm_vm_close(replacement);
    assert(tm_vm_fft_resume(a)==TM_VM_OK&&tm_vm_fft_status(a)==TM_FFT_ACTIVE);
    tick(a); assert(value(a,0)==3&&value(a,1)==512&&value(a,3)==205);
    tm_vm_close(a); tm_vm_close(b);
    tm_fft_config default_input={0}; assert(!tm_fft_configure(&default_input,NULL));
    tm_vm* default_vm=NULL; assert(tm_vm_open_configured(&default_vm,bytes,byte_count,&default_input)==TM_VM_OK);
    assert(tm_vm_fft_status(default_vm)==TM_FFT_ACTIVE); tick(default_vm); assert(value(default_vm,1)==512); tm_vm_close(default_vm);
    tm_vm* missing=open_vm("missing",TM_FFT_UNAVAILABLE); tick(missing); assert(!value(missing,1)); tm_vm_close(missing);
    tm_fft_config slow={0}; assert(!tm_fft_configure(&slow,"Mic C"));
    tm_vm* timed_out=NULL; assert(tm_vm_open_configured(&timed_out,bytes,byte_count,&slow)==TM_VM_TIMEOUT&&!timed_out);
    assert(descriptors()==baseline);
}
static void studio_cases(void)
{
    tm_fft_config config={0}; assert(!tm_fft_configure(&config,"Mic A"));
    tm_studio_session* session=NULL;
    assert(tm_studio_session_open_configured(&session,directory,NULL,&config)==TM_STUDIO_OK);
    assert(tm_studio_session_fft_status(session)==TM_FFT_ACTIVE);
    memset(&config,0xa5,sizeof config);
    assert(tm_studio_session_load(session,bytes,byte_count,"FFT.tic")==TM_STUDIO_OK);
    assert(tm_studio_session_run(session,5000)==TM_STUDIO_OK);
    assert(tm_studio_session_persistent(session)[1]==512);
    pid_t old=tm_studio_session_pid(session); assert(old>0&&!kill(old,SIGKILL));
    assert(tm_studio_session_tick(session,(tic80_input){0},0,5000)==TM_STUDIO_RECOVERED);
    assert(tm_studio_session_pid(session)!=old&&tm_studio_session_fft_status(session)==TM_FFT_ACTIVE);
    assert(tm_studio_session_run(session,5000)==TM_STUDIO_OK);
    assert(tm_studio_session_persistent(session)[1]==512);
    assert(tm_studio_session_close(session)==TM_STUDIO_OK);
    assert(!tm_fft_configure(&config,"missing")); session=NULL;
    assert(tm_studio_session_open_configured(&session,directory,NULL,&config)==TM_STUDIO_OK);
    assert(tm_studio_session_fft_status(session)==TM_FFT_UNAVAILABLE);
    assert(tm_studio_session_load(session,bytes,byte_count,"FFT.tic")==TM_STUDIO_OK);
    assert(tm_studio_session_run(session,5000)==TM_STUDIO_OK);
    assert(!tm_studio_session_persistent(session)[1]); assert(tm_studio_session_close(session)==TM_STUDIO_OK);
}
static void playback_parity(void)
{
    tic_cartridge* cart=calloc(1,sizeof *cart); assert(cart);
    strcpy(cart->code.data,"-- script: lua\nn=0 function TIC() if n==0 then sfx(0,'C-4',8,0,{15,7},0) end pmem(10,math.floor(fftr(32)+0.5)) cls(n%16) n=n+1 end\n");
    for(unsigned i=0;i<WAVE_VALUES;++i) tic_tool_poke4(cart->banks[0].sfx.waveforms.items[0].data,i,i<WAVE_VALUES/2?15:0);
    cart->banks[0].sfx.samples.data[0].octave=4;
    u8* data=malloc(sizeof *cart*2); assert(data); s32 size=tic_cart_save(cart,data); assert(size>0); free(cart);
    tm_fft_config config={0}; assert(!tm_fft_configure(&config,"Mic A"));
    tm_vm *capturing=NULL,*silent_input=NULL;
    assert(tm_vm_open_configured(&capturing,data,size,&config)==TM_VM_OK&&tm_vm_fft_status(capturing)==TM_FFT_ACTIVE);
    assert(tm_vm_open(&silent_input,data,size)==TM_VM_OK&&tm_vm_fft_status(silent_input)==TM_FFT_DISABLED);
    free(data); unsigned nonzero=0;
    for(unsigned frame=0;frame<16;++frame) {
        assert(tm_vm_tick(capturing,(tic80_input){0},0)==TM_VM_OK);
        assert(tm_vm_tick(silent_input,(tic80_input){0},0)==TM_VM_OK);
        tic80* a=tm_vm_product(capturing); tic80* b=tm_vm_product(silent_input);
        assert(a->samples.count==1600&&b->samples.count==1600);
        assert(!memcmp(a->samples.buffer,b->samples.buffer,1600*sizeof(s16)));
        assert(!memcmp(a->screen,b->screen,TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4));
        assert(value(capturing,10)==512&&!value(silent_input,10));
        for(unsigned i=0;i<1600;++i) nonzero+=a->samples.buffer[i]!=0;
    }
    assert(nonzero); tm_vm_close(capturing); tm_vm_close(silent_input);
    printf("Capture playback parity: frames=16 samples=25600 nonzero_samples=%u\n",nonzero);
}
static void validation(void)
{
    assert(tm_fft_supported());
    tm_fft_config config={0},before;
    assert(tm_fft_config_valid(&config)); assert(!tm_fft_configure(&config,NULL));
    before=config; char oversized[TM_FFT_DEVICE_BYTES+1]; memset(oversized,'x',sizeof oversized); oversized[sizeof oversized-1]=0;
    assert(tm_fft_configure(&config,"")<0&&!memcmp(&config,&before,sizeof config));
    assert(tm_fft_configure(&config,oversized)<0&&!memcmp(&config,&before,sizeof config));
    config.enabled=2; assert(!tm_fft_config_valid(&config));
    tm_vm* vm=(void*)1; assert(tm_vm_open_configured(&vm,bytes,byte_count,&config)==TM_VM_ERROR&&!vm);
    tm_studio_session* session=(void*)1;
    assert(tm_studio_session_open_configured(&session,directory,NULL,&config)==TM_STUDIO_ERROR&&!session);
    config.enabled=1; memset(config.device,'x',sizeof config.device); assert(!tm_fft_config_valid(&config));
    config=(tm_fft_config){0}; config.device[0]='x'; assert(!tm_fft_config_valid(&config));
}
static void check_events(void)
{
    FILE* file=fopen(logfile,"r"); assert(file); char name[64]; long pid; int bin;
    unsigned opens=0,closes=0,stalls=0;
    while(fscanf(file,"%ld %63s %d",&pid,name,&bin)==3) {
        assert(pid!=getpid());
        opens+=!strcmp(name,"device-open"); closes+=!strcmp(name,"device-close"); stalls+=!strcmp(name,"start-stall");
    }
    fclose(file);
    /* One live Studio worker was deliberately SIGKILLed; its lock/fds must be
     * released by the OS so recovery can reopen the same exclusive device. */
    assert(opens==closes+1&&stalls==1);
    printf("Capture device events: opened=%u closed=%u killed_with_device=1 initialization_stalls=%u supervisor_opens=0\n",opens,closes,stalls);
    assert(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD);
}
static int remove_owned(const char* path,const struct stat* info,int type,struct FTW* walk)
{ (void)info; (void)type; (void)walk; return remove(path); }
int main(int argc,char** argv)
{
    if(argc>=2&&!strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    if(argc>=2&&!strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    assert(argc==1);
    char pattern[]="/tmp/tic80-fft-worker-XXXXXX"; assert(mkdtemp(pattern)); strcpy(directory,pattern);
    assert(snprintf(logfile,sizeof logfile,"%s/events.log",directory)<sizeof logfile); assert(!setenv("TM_TEST_FFT_LOG",logfile,1));
    tic_cartridge* cart=calloc(1,sizeof *cart); assert(cart); strcpy(cart->code.data,code);
    bytes=malloc(sizeof *cart*2); assert(bytes); byte_count=tic_cart_save(cart,bytes); assert(byte_count>0); free(cart);
    validation(); vm_cases(); playback_parity(); studio_cases(); check_events(); free(bytes);
    assert(!nftw(directory,remove_owned,16,FTW_DEPTH|FTW_PHYS));
    puts("Configured exec-worker capture: actual FFT data, isolation, exclusivity, disabled/unavailable states, timeout cleanup and Studio recovery passed");
    return 0;
}
