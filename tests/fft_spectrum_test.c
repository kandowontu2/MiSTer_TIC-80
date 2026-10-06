/* Reuse only the deterministic miniaudio boundary and its lifecycle helpers.
 * The numerical oracle below never calls Kiss FFT or production query loops. */
#define main lifecycle_fixture_main
#include "fft_capture_test.c"
#undef main
#include <float.h>

enum { N = FFT_SIZE * 2 };
static double reference_raw[FFT_SIZE],reference_smooth[FFT_SIZE];
static double reference_normal[FFT_SIZE],reference_normal_smooth[FFT_SIZE];
static double held_peak;
static double raw_tolerance;
static unsigned frames,queries;

static void match(double actual,double expected,const char* label,int bin,double absolute)
{
    if(!isfinite(actual)||fabs(actual-expected)>absolute+fabs(expected)*0.00015) {
        fprintf(stderr,"%s frame=%u bin=%d expected=%.17g actual=%.17g\n",label,frames,bin,expected,actual);
        abort();
    }
}
static void finite_outputs(void)
{
    for(int i=0;i<FFT_SIZE;++i) {
        assert(isfinite(fftData[i])&&fftData[i]>=0&&fftData[i]<=1.00001f);
        assert(isfinite(fftSmoothingData[i])&&fftSmoothingData[i]>=0&&fftSmoothingData[i]<=1.00001f);
        assert(isfinite(fftRawData[i])&&fftRawData[i]>=0);
        assert(isfinite(fftRawSmoothingData[i])&&fftRawSmoothingData[i]>=0);
    }
    assert(isfinite(fPeakSmoothValue)&&fPeakSmoothValue>=0.01f);
    assert(isfinite(fAmplification)&&fAmplification>0);
}
static void direct_dft(const float* mono)
{
    const long double tau=2*acosl(-1.0L);
    for(int k=0;k<FFT_SIZE;++k) {
        long double real=0,imag=0,c=1,s=0;
        long double step_c=cosl(tau*k/N),step_s=-sinl(tau*k/N);
        for(int j=0;j<N;++j) {
            real+=mono[j]*c; imag+=mono[j]*s;
            long double next=c*step_c-s*step_s;
            s=s*step_c+c*step_s; c=next;
        }
        reference_raw[k]=(double)(2*hypotl(real,imag));
    }
}
static void check_queries(void)
{
    /* Contract cases are expressed as expected inclusive bin intervals. */
    static const int cases[][4]={
        {0,-1,0,0},{32,-1,32,32},{1023,-1,1023,1023},
        {-1,-1,-1,-1},{1024,-1,-1,-1},{0,1023,0,1023},
        {31,64,31,64},{-20,3,0,3},{1031,8,1023,1023},
        {INT_MIN,INT_MAX,0,1023},{INT_MAX,INT_MIN,1023,1023},
        {12,3,12,12},{-4,-2,-1,-1},{1024,2048,-1,-1}};
    query api[]={tic_api_fft,tic_api_ffts,tic_api_fftr,tic_api_fftrs};
    double* expected[]={reference_normal,reference_normal_smooth,reference_raw,reference_smooth};
    for(int at=0;at<4;++at) {
        double prefix[FFT_SIZE+1]={0};
        for(int i=0;i<FFT_SIZE;++i) prefix[i+1]=prefix[i]+expected[at][i];
        for(unsigned i=0;i<sizeof cases/sizeof cases[0];++i) {
            const int* c=cases[i]; double value=c[2]<0?0:prefix[c[3]+1]-prefix[c[2]];
            double absolute=at<2?0.00004:raw_tolerance*(c[2]<0?1:c[3]-c[2]+1);
            match(api[at](NULL,c[0],c[1]),value,"query mismatch",c[0],absolute);
            ++queries;
        }
    }
}
static void process(const float* stereo)
{
    float mono[AUDIO_BUFFER_SIZE]; feed(stereo,AUDIO_BUFFER_SIZE); FFT_CopyAudio(mono,AUDIO_BUFFER_SIZE);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) {
        double left=stereo[2*i],right=stereo[2*i+1];
        if(!isfinite(left)) left=0; if(!isfinite(right)) right=0;
        float expected=(float)((left+right)/2);
        if(mono[i]!=expected) {
            fprintf(stderr,"capture sample mismatch frame=%u sample=%d expected=%g actual=%g\n",frames,i,expected,mono[i]); abort();
        }
    }
    direct_dft(mono+AUDIO_BUFFER_SIZE-N);
    double peak=0.01;
    for(int i=0;i<FFT_SIZE;++i) if(reference_raw[i]>peak) peak=reference_raw[i];
    held_peak=peak>held_peak?peak:held_peak*0.995+peak*0.005;
    raw_tolerance=fmax(0.002,held_peak*0.000003);
    for(int i=0;i<FFT_SIZE;++i) {
        reference_normal[i]=reference_raw[i]/held_peak;
        reference_smooth[i]=reference_smooth[i]*0.6+reference_raw[i]*0.4;
        reference_normal_smooth[i]=reference_normal_smooth[i]*0.6+reference_normal[i]*0.4;
    }
    FFT_GetFFT(fftData);
    for(int i=0;i<FFT_SIZE;++i) {
        match(fftRawData[i],reference_raw[i],"raw spectrum mismatch",i,raw_tolerance);
        match(fftRawSmoothingData[i],reference_smooth[i],"raw smoothing mismatch",i,raw_tolerance);
        match(fftData[i],reference_normal[i],"normalized spectrum mismatch",i,0.00003);
        match(fftSmoothingData[i],reference_normal_smooth[i],"normalized smoothing mismatch",i,0.00003);
    }
    finite_outputs(); check_queries(); ++frames;
}
static void reset(void)
{
    assert(FFT_Open(false,NULL)); held_peak=0;
    memset(reference_smooth,0,sizeof reference_smooth);
    memset(reference_normal_smooth,0,sizeof reference_normal_smooth);
}
static void ordinary_spectra(void)
{
    reset(); float stereo[AUDIO_BUFFER_SIZE*2]; unsigned random=0x7a19b63d;
    const double tau=2*acos(-1.0);
    for(int signal=0;signal<12;++signal) {
        for(int i=0;i<AUDIO_BUFFER_SIZE;++i) {
            int j=i%N; double v=0;
            switch(signal) {
            case 0: break;
            case 1: v=0.25*cos(tau*32*j/N); break;
            case 2: v=0.00025*cos(tau*32*j/N); break;
            case 3: v=0.5*sin(tau*731*j/N); break;
            case 4: v=0.02+0.125*cos(tau*1023*j/N); break;
            case 5: v=(j&1)?-0.75:0.75; break; /* excluded Nyquist bin */
            case 6: v=j==1023?1:0; break;
            case 7:
                random^=random<<13; random^=random>>17; random^=random<<5;
                v=((double)(random&0xffff)/65535-0.5)*0.5; break;
            case 8: v=0.1*sin(tau*7.25*j/N)+0.2*cos(tau*200.5*j/N)+0.05*sin(tau*719.875*j/N); break;
            case 9: v=-0.25; break;
            case 10: v=0.5*cos(tau*47*j/N); break;
            case 11: v=nextafterf(0,1); break;
            }
            stereo[2*i]=(float)v; stereo[2*i+1]=signal==10?(float)-v:(float)v;
        }
        process(stereo);
    }
}
static void large_magnitudes(void)
{
    reset(); float stereo[AUDIO_BUFFER_SIZE*2]; const double tau=2*acos(-1.0);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) stereo[2*i]=stereo[2*i+1]=(float)(1e20*cos(tau*32*i/N));
    process(stereo); assert(fftRawData[32]>1e23f);
}
static void invalid_input(void)
{
    reset(); float stereo[AUDIO_BUFFER_SIZE*2];
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) {
        static const float bad[]={NAN,INFINITY,-INFINITY};
        stereo[2*i]=bad[i%3]; stereo[2*i+1]=0.25f;
    }
    process(stereo); assert(fftRawData[0]==512);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) stereo[2*i]=stereo[2*i+1]=FLT_MAX;
    feed(stereo,AUDIO_BUFFER_SIZE);
    float mono[AUDIO_BUFFER_SIZE]; FFT_CopyAudio(mono,AUDIO_BUFFER_SIZE);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) assert(mono[i]==FLT_MAX);
    FFT_GetFFT(fftData); finite_outputs();
    VQT_ProcessAudio();
    for(int i=0;i<VQT_BINS;++i) assert(isfinite(vqtData[i])&&isfinite(vqtNormalizedData[i]));
    /* Recovery is in the same open device, after completely replacing input. */
    const double tau=2*acos(-1.0);
    for(int i=0;i<AUDIO_BUFFER_SIZE;++i) stereo[2*i]=stereo[2*i+1]=(float)(0.25*cos(tau*32*i/N));
    feed(stereo,AUDIO_BUFFER_SIZE); FFT_GetFFT(fftData); finite_outputs();
    near_value(fftRawData[32],512); assert(fftData[32]>0.9f);
    VQT_ProcessAudio(); bool nonzero=false;
    for(int i=0;i<VQT_BINS;++i) { assert(isfinite(vqtData[i])&&isfinite(vqtNormalizedData[i])); nonzero|=vqtData[i]>0; }
    assert(nonzero);
}
int main(int argc,char** argv)
{
    g_currentLogLevel=FFT_LOG_OFF;
    if(argc==2&&!strcmp(argv[1],"--normalization")) ordinary_spectra();
    else if(argc==2&&!strcmp(argv[1],"--nonfinite")) invalid_input();
    else if(argc==2&&!strcmp(argv[1],"--magnitude")) large_magnitudes();
    else { assert(argc==1); ordinary_spectra(); large_magnitudes(); invalid_input(); }
    FFT_Close(); clean(); assert(close_count==initialize_count);
    printf("FFT spectrum oracle passed: DFT_frames=%u bins_per_frame=1024 APIs=4 queries=%u; startup/level changes, silence, Nyquist, impulse, noise, large magnitudes and invalid-input recovery\n",frames,queries);
    return 0;
}
