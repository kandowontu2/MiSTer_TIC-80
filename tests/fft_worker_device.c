/* Deterministic device boundary for exec-worker integration. The FFT/VQT
 * implementation, worker IPC, lifecycle and cartridge callbacks are real. */
#define _GNU_SOURCE
#include "ext/miniaudio.h"
#include "fftdata.h"
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
static ma_log* logger;
static ma_context* owner;
static ma_device* device;
static ma_device_data_proc callback;
static int lock_fd=-1,bin;
static const char *log_path(void) { const char* path=getenv("TM_TEST_FFT_LOG"); assert(path&&*path); return path; }
static void event(const char* name)
{
    int fd=open(log_path(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600); assert(fd>=0);
    char line[96]; int size=snprintf(line,sizeof line,"%ld %s %d\n",(long)getpid(),name,bin);
    assert(write(fd,line,(size_t)size)==size); close(fd);
}
ma_log_callback ma_log_callback_init(ma_log_callback_proc proc,void* data)
{ ma_log_callback result={proc,data}; return result; }
ma_result ma_log_init(const ma_allocation_callbacks* unused,ma_log* value)
{ (void)unused; assert(!logger); logger=value; memset(value,0,sizeof *value); event("logger-open"); return MA_SUCCESS; }
void ma_log_uninit(ma_log* value)
{ assert(logger==value&&!owner&&!device); logger=NULL; event("logger-close"); }
ma_result ma_log_register_callback(ma_log* value,ma_log_callback cb)
{ assert(value==logger); value->callbacks[0]=cb; value->callbackCount=1; return MA_SUCCESS; }
ma_context_config ma_context_config_init(void) { ma_context_config value={0}; return value; }
ma_result ma_context_init(const ma_backend* backends,ma_uint32 count,const ma_context_config* config,ma_context* value)
{
    assert(count==1&&backends[0]==ma_backend_alsa&&config->pLog==logger&&!owner);
    owner=value; memset(value,0,sizeof *value); value->backend=ma_backend_alsa; event("context-open"); return MA_SUCCESS;
}
ma_result ma_context_uninit(ma_context* value)
{ assert(value==owner&&!device); owner=NULL; event("context-close"); return MA_SUCCESS; }
ma_result ma_context_get_devices(ma_context* value,ma_device_info** playback,ma_uint32* np,ma_device_info** capture,ma_uint32* nc)
{
    assert(value==owner&&logger->callbackCount==1);
    static ma_device_info infos[3]; memset(infos,0,sizeof infos);
    for(int i=0;i<3;++i) { snprintf(infos[i].name,sizeof infos[i].name,"Mic %c",'A'+i); snprintf(infos[i].id.alsa,sizeof infos[i].id.alsa,"%c",'A'+i); }
    *playback=NULL; *np=0; *capture=infos; *nc=3; return MA_SUCCESS;
}
ma_device_config ma_device_config_init(ma_device_type type) { ma_device_config config={0}; config.deviceType=type; return config; }
ma_result ma_device_init(ma_context* value,const ma_device_config* config,ma_device* target)
{
    assert(value==owner&&!device&&config->deviceType==ma_device_type_capture);
    assert(config->sampleRate==44100&&config->capture.channels==2&&config->capture.format==ma_format_f32);
    char selected=config->capture.pDeviceID?config->capture.pDeviceID->alsa[0]:'A';
    assert(selected>='A'&&selected<='C'); bin=selected=='B'?64:32;
    if(selected=='C') { event("start-stall"); sleep(15); }
    char path[4096]; assert(snprintf(path,sizeof path,"%s.%c.lock",log_path(),selected)<sizeof path);
    lock_fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC,0600); assert(lock_fd>=0);
    if(flock(lock_fd,LOCK_EX|LOCK_NB)) { close(lock_fd); lock_fd=-1; event("device-busy"); return MA_ERROR; }
    device=target; callback=config->dataCallback;
    memset(target,0,sizeof *target); target->pContext=value; event("device-open"); return MA_SUCCESS;
}
ma_result ma_device_start(ma_device* target)
{
    assert(target==device);
    float samples[AUDIO_BUFFER_SIZE*2];
    double pi=acos(-1.0);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) {
        double tone=0.25*cos(2*pi*bin*i/2048);
        samples[2*i]=(float)(tone+0.125); samples[2*i+1]=(float)(tone-0.125);
    }
    callback(target,NULL,samples,AUDIO_BUFFER_SIZE); event("device-start"); return MA_SUCCESS;
}
void ma_device_uninit(ma_device* target)
{ assert(target==device&&lock_fd>=0); device=NULL; callback=NULL; close(lock_fd); lock_fd=-1; event("device-close"); }
ma_result ma_spinlock_lock(volatile ma_spinlock* value)
{ while(__atomic_exchange_n(value,1,__ATOMIC_ACQUIRE)) {} return MA_SUCCESS; }
ma_result ma_spinlock_unlock(volatile ma_spinlock* value)
{ __atomic_store_n(value,0,__ATOMIC_RELEASE); return MA_SUCCESS; }
