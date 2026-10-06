/* Fault the device boundary while running the real callback, FFT and VQT.
 * This proves cleanup/math, not that native ALSA has a usable microphone. */
#include "api.h"
#include "ext/fft.h"
#include "fftdata.h"
#include "ext/vqt.h"
#include "ext/kiss_fftr.h"
#include "ext/miniaudio.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_stage, allocation_countdown;
static ma_log* logs[2];
static ma_context* contexts[2];
static ma_log* context_logs[2];
static ma_device* active_device;
static ma_device_data_proc callback;
static ma_device_info device_info;
static unsigned close_count, initialize_count;

static int log_slot(const ma_log* pointer)
{ for (int i=0;i<2;++i) if(logs[i]==pointer) return i; return -1; }
static int context_slot(const ma_context* pointer)
{ for (int i=0;i<2;++i) if(contexts[i]==pointer) return i; return -1; }
static void clean(void)
{
    assert(!logs[0]&&!logs[1]&&!contexts[0]&&!contexts[1]&&!active_device);
    assert(!fftEnabled&&!vqtEnabled);
}
ma_log_callback ma_log_callback_init(ma_log_callback_proc proc, void* data)
{ ma_log_callback result={proc,data}; return result; }
ma_result ma_log_init(const ma_allocation_callbacks* unused, ma_log* log)
{
    (void)unused;
    if(fail_stage==1) return MA_ERROR;
    int slot=logs[0]?1:0; assert(!logs[slot]); logs[slot]=log;
    memset(log,0,sizeof(*log)); return MA_SUCCESS;
}
void ma_log_uninit(ma_log* log)
{
    int slot=log_slot(log); assert(slot>=0);
    for(int i=0;i<2;++i) assert(!contexts[i]||context_logs[i]!=log);
    logs[slot]=NULL;
}
ma_result ma_log_register_callback(ma_log* log, ma_log_callback value)
{
    assert(log_slot(log)>=0);
    if(fail_stage==2) return MA_ERROR;
    log->callbacks[0]=value; log->callbackCount=1; return MA_SUCCESS;
}
ma_context_config ma_context_config_init(void)
{ ma_context_config result={0}; return result; }
ma_result ma_context_init(const ma_backend* backends, ma_uint32 count,
    const ma_context_config* config, ma_context* context)
{
    assert(count==1&&backends&&backends[0]==ma_backend_alsa);
    assert(log_slot(config->pLog)>=0);
    if(fail_stage==3) return MA_ERROR;
    int slot=contexts[0]?1:0; assert(!contexts[slot]);
    contexts[slot]=context; context_logs[slot]=config->pLog;
    memset(context,0,sizeof(*context)); context->backend=ma_backend_alsa;
    return MA_SUCCESS;
}
ma_result ma_context_uninit(ma_context* context)
{
    int slot=context_slot(context); assert(slot>=0);
    assert(!active_device||active_device->pContext!=context);
    assert(log_slot(context_logs[slot])>=0);
    contexts[slot]=NULL; context_logs[slot]=NULL; return MA_SUCCESS;
}
ma_result ma_context_get_devices(ma_context* context, ma_device_info** playback,
    ma_uint32* playback_count, ma_device_info** capture, ma_uint32* capture_count)
{
    int slot=context_slot(context); assert(slot>=0);
    assert(context_logs[slot]->callbackCount==1);
    if(fail_stage==4) return MA_ERROR;
    memset(&device_info,0,sizeof device_info);
    strcpy(device_info.name,"USB Test Microphone");
    strcpy(device_info.id.alsa,"hw:1,0");
    *playback=NULL; *playback_count=0; *capture=&device_info; *capture_count=1;
    return MA_SUCCESS;
}
ma_device_config ma_device_config_init(ma_device_type type)
{ ma_device_config result={0}; result.deviceType=type; return result; }
ma_result ma_device_init(ma_context* context, const ma_device_config* config, ma_device* device)
{
    assert(context_slot(context)>=0);
    assert(config->deviceType==ma_device_type_capture&&config->capture.format==ma_format_f32);
    assert(config->capture.channels==2&&config->sampleRate==44100);
    if(config->capture.pDeviceID) assert(!strcmp(config->capture.pDeviceID->alsa,"hw:1,0"));
    if(fail_stage==5) return MA_ERROR;
    assert(!active_device); active_device=device; callback=config->dataCallback;
    memset(device,0,sizeof(*device)); device->pContext=context;
    ++initialize_count; return MA_SUCCESS;
}
ma_result ma_device_start(ma_device* device)
{ assert(device==active_device); return fail_stage==6?MA_ERROR:MA_SUCCESS; }
/* Referenced only by the retained old-source negative control. */
ma_result ma_device_stop(ma_device* device)
{ assert(device==active_device); return MA_SUCCESS; }
ma_bool32 ma_is_loopback_supported(ma_backend backend)
{ (void)backend; return MA_FALSE; }
const char* ma_get_backend_name(ma_backend backend)
{ (void)backend; return "alsa"; }
const char* ma_result_description(ma_result result)
{ (void)result; return "injected device failure"; }
void ma_device_uninit(ma_device* device)
{ assert(device==active_device); active_device=NULL; callback=NULL; ++close_count; }
ma_result ma_spinlock_lock(volatile ma_spinlock* lock)
{ while(__atomic_exchange_n(lock,1,__ATOMIC_ACQUIRE)) {} return MA_SUCCESS; }
ma_result ma_spinlock_unlock(volatile ma_spinlock* lock)
{ __atomic_store_n(lock,0,__ATOMIC_RELEASE); return MA_SUCCESS; }

kiss_fftr_cfg __real_kiss_fftr_alloc(int,int,void*,size_t*);
kiss_fftr_cfg __wrap_kiss_fftr_alloc(int n,int inverse,void* memory,size_t* size)
{
    if(allocation_countdown&&!--allocation_countdown) return NULL;
    return __real_kiss_fftr_alloc(n,inverse,memory,size);
}
static void near_value(double actual,double expected)
{ assert(isfinite(actual)); assert(fabs(actual-expected)<0.003+fabs(expected)*0.0001); }
static void feed(const float* data,unsigned count)
{ assert(active_device&&callback); callback(active_device,NULL,data,count); }
static void failures(void)
{
    FFT_Close(); FFT_Close(); clean();
    for(int stage=1;stage<=6;++stage)
    {
        fail_stage=stage; assert(!FFT_Open(false,"USB Test")); clean();
        FFT_Close(); clean(); fail_stage=0;
        assert(FFT_Open(false,"USB Test")); assert(fftEnabled&&vqtEnabled);
        FFT_Close(); clean();
    }
    allocation_countdown=1; assert(!FFT_Open(false,NULL)); clean();
    allocation_countdown=2; assert(FFT_Open(false,NULL));
    assert(fftEnabled&&!vqtEnabled); FFT_Close(); clean();
    assert(!FFT_Open(true,NULL)); clean();
    assert(!FFT_Open(false,"missing microphone")); clean();
    float zeros[FFT_SIZE]; for(int i=0;i<FFT_SIZE;++i) zeros[i]=3;
    FFT_GetFFT(zeros); for(int i=0;i<FFT_SIZE;++i) assert(zeros[i]==0);
    FFT_GetFFT(NULL); FFT_CopyAudio(NULL,10);
}
static void enumeration(void)
{
    assert(FFT_Open(false,NULL));
    ma_device* before=active_device;
    unsigned initialized=initialize_count,closed=close_count;
    for(int stage=0;stage<=4;++stage)
    {
        fail_stage=stage; FFT_EnumerateDevices();
        assert(active_device==before&&fftEnabled&&vqtEnabled);
        assert(initialize_count==initialized&&close_count==closed);
        int slot=context_slot(before->pContext); assert(slot>=0);
        /* Dereference after FFT_Open returned: ASAN catches a stack logger. */
        assert(context_logs[slot]->callbackCount==1);
        assert((logs[0]!=NULL)+(logs[1]!=NULL)==1);
        assert((contexts[0]!=NULL)+(contexts[1]!=NULL)==1);
    }
    fail_stage=0; FFT_Close(); clean();
    for(int stage=0;stage<=4;++stage)
    { fail_stage=stage; FFT_EnumerateDevices(); clean(); }
    fail_stage=0;
}
static void pcm_and_math(void)
{
    assert(FFT_Open(false,NULL));
    float stereo[(AUDIO_BUFFER_SIZE+17)*2],mono[AUDIO_BUFFER_SIZE];
    for(int i=0;i<AUDIO_BUFFER_SIZE+17;++i) { stereo[2*i]=i; stereo[2*i+1]=i*3; }
    feed(stereo,AUDIO_BUFFER_SIZE+17); FFT_CopyAudio(mono,AUDIO_BUFFER_SIZE);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) assert(mono[i]==2*(i+17));
    feed(NULL,31); FFT_CopyAudio(mono,AUDIO_BUFFER_SIZE);
    for(int i=0;i<AUDIO_BUFFER_SIZE-31;++i) assert(mono[i]==2*(i+48));
    for(int i=AUDIO_BUFFER_SIZE-31;i<AUDIO_BUFFER_SIZE;++i) assert(mono[i]==0);
    mono[0]=12; FFT_CopyAudio(mono,0); FFT_CopyAudio(mono,AUDIO_BUFFER_SIZE+1); assert(mono[0]==12);
    assert(FFT_Open(false,NULL)); /* replacement closes and resets prior capture */
    FFT_CopyAudio(mono,AUDIO_BUFFER_SIZE);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) assert(mono[i]==0);
    const double pi=acos(-1.0);
    for(int i=0;i<FFT_SIZE*2;++i)
    {
        double signal=0.125+0.5*cos(2*pi*31*i/(FFT_SIZE*2))+0.25*sin(2*pi*64*i/(FFT_SIZE*2));
        stereo[2*i]=(float)(signal+0.1); stereo[2*i+1]=(float)(signal-0.1);
    }
    feed(stereo,FFT_SIZE*2);
    FFT_GetFFT(fftData);
    near_value(fftRawData[0],512); near_value(fftRawData[31],1024); near_value(fftRawData[64],512);
    for(int i=0;i<FFT_SIZE;++i)
    {
        double expected=i==0||i==64?512:i==31?1024:0;
        near_value(fftRawData[i],expected); near_value(fftData[i],expected/1024);
        near_value(fftRawSmoothingData[i],expected*0.4);
        near_value(fftSmoothingData[i],expected*0.4/1024);
    }
    FFT_GetFFT(fftData); near_value(fftData[31],1); near_value(fftData[64],0.5);
    near_value(fftRawSmoothingData[31],1024*0.64);
    near_value(fftSmoothingData[31],0.64);
    VQT_ProcessAudio();
    bool nonzero=false;
    for(int i=0;i<VQT_BINS;++i)
    { assert(isfinite(vqtData[i])&&vqtData[i]>=0); nonzero|=vqtData[i]>0; }
    assert(nonzero);
    FFT_Close(); clean();
    for(int i=0;i<FFT_SIZE;++i) assert(!fftData[i]&&!fftRawData[i]&&!fftSmoothingData[i]&&!fftRawSmoothingData[i]);
}
typedef double(*query)(tic_mem*,s32,s32);
static int producer_start, producer_done;
static void* produce(void* unused)
{
    (void)unused;
    float block[AUDIO_BUFFER_SIZE*2];
    while(!__atomic_load_n(&producer_start,__ATOMIC_ACQUIRE)) {}
    for(int value=1;value<=200;++value)
    {
        for(int i=0;i<AUDIO_BUFFER_SIZE*2;++i) block[i]=(float)value;
        feed(block,AUDIO_BUFFER_SIZE);
    }
    __atomic_store_n(&producer_done,1,__ATOMIC_RELEASE); return NULL;
}
static void concurrent_capture(void)
{
    assert(FFT_Open(false,NULL));
    pthread_t thread; assert(!pthread_create(&thread,NULL,produce,NULL));
    float samples[AUDIO_BUFFER_SIZE]; unsigned snapshots=0;
    __atomic_store_n(&producer_start,1,__ATOMIC_RELEASE);
    do
    {
        FFT_CopyAudio(samples,AUDIO_BUFFER_SIZE);
        for(int i=1;i<AUDIO_BUFFER_SIZE;++i) assert(samples[i]==samples[0]);
        assert(samples[0]>=0&&samples[0]<=200); ++snapshots;
    } while(!__atomic_load_n(&producer_done,__ATOMIC_ACQUIRE));
    assert(!pthread_join(thread,NULL)); assert(snapshots);
    FFT_CopyAudio(samples,AUDIO_BUFFER_SIZE);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) assert(samples[i]==200);
    FFT_Close(); clean();
}
static void ranges(void)
{
    assert(FFT_Open(false,NULL));
    const int cases[][2]={{0,-1},{1023,-1},{-1,-1},{1024,-1},{INT_MIN,INT_MAX},
        {INT_MAX,INT_MIN},{1024,4},{-4,5},{12,3},{-4,-2},{1024,2048},{0,1023},{1020,2048}};
    query queries[]={tic_api_fft,tic_api_ffts,tic_api_fftr,tic_api_fftrs};
    float* arrays[]={fftData,fftSmoothingData,fftRawData,fftRawSmoothingData};
    for(int api=0;api<4;++api)
    {
        for(int i=0;i<FFT_SIZE;++i) arrays[api][i]=(api+1)*(i+1)*0.25f;
        for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c)
        {
            int a=cases[c][0],b=cases[c][1]; double expected=0;
            if(b==-1) { if(a>=0&&a<1024) expected=(api+1)*(a+1)*0.25; }
            else if(!((a<0&&b<0)||(a>=1024&&b>=1024)))
            {
                if(a<0) a=0; if(a>=1024) a=1023; if(b>=1024) b=1023; if(a>b) b=a;
                /* Closed-form sum is independent of the production bin loop. */
                expected=(api+1)*0.25*((double)(a+b+2)*(b-a+1)/2);
            }
            near_value(queries[api](NULL,cases[c][0],cases[c][1]),expected);
        }
    }
    FFT_Close(); clean();
    for(int api=0;api<4;++api) assert(queries[api](NULL,0,1023)==0);
}
int main(int argc,char** argv)
{
    g_currentLogLevel=FFT_LOG_OFF;
    if(argc==2&&!strcmp(argv[1],"--ranges")) ranges();
    else if(argc==2&&!strcmp(argv[1],"--context-failure"))
    { fail_stage=3; assert(!FFT_Open(false,NULL)); clean(); }
    else { assert(argc==1); failures(); enumeration(); pcm_and_math(); concurrent_capture(); ranges(); }
    assert(close_count==initialize_count);
    puts("FFT capture lifecycle, PCM, real FFT/VQT and 52 range cases passed"); return 0;
}
