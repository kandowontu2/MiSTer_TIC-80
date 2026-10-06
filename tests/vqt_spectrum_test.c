/* Independent transform/kernel/matrix and whitening checks, with only the
 * capture-device boundary doubled. Production Kiss FFT and VQT are exercised. */
#define main capture_lifecycle_main
#include "fft_capture_test.c"
#undef main
#include "ext/vqt_kernel.h"
#include <float.h>
#include <complex.h>

enum { VN=VQT_FFT_SIZE, VB=VQT_BINS, FN=VN/2+1 };
static long double complex spectrum[VN];
static double raw[VB], smooth[VB], white[VB], white_smooth[VB];
static double peak=1, white_peak=1;
static unsigned frames, api_queries, coefficients, sparse_coefficients;
static double max_raw_error, max_white_error;

static void close_to(double actual,double expected,double absolute,double relative,const char* label,int bin)
{
    if(!isfinite(actual)||fabs(actual-expected)>absolute+fabs(expected)*relative) {
        fprintf(stderr,"%s frame=%u bin=%d expected=%.17g actual=%.17g\n",label,frames,bin,expected,actual);
        abort();
    }
}

/* A finite geometric sum, independent of Kiss FFT and generated kernel data. */
static long double complex geometric(long double angle,int n)
{
    const long double tau=2*acosl(-1.0L);
    angle=remainderl(angle,tau);
    long double half=angle/2;
    if(fabsl(half)<1e-18L) return n;
    return sinl(n*half)/sinl(half)*cexpl(-I*half*(n-1));
}
static long double complex window_transform(long double angle,int length)
{
    long double beta=2*acosl(-1.0L)/(length-1);
    return (long double)0.54f*geometric(angle,length)
        -(long double)0.46f/2*(geometric(angle-beta,length)+geometric(angle+beta,length));
}
static long double q_policy(long double f)
{
    /* The pinned musical policy, including its low-frequency truncation. */
    const double edges[]={25,30,40,50,65,80,160,320,640};
    const float values[]={7.4f,9.2f,11.5f,14.5f,16,17,17,15,13,11};
    unsigned at=0; while(at<sizeof edges/sizeof *edges && f>=edges[at]) ++at;
    return values[at];
}
static void check_kernels(void)
{
    float actual_frequencies[VB];
    VQT_GenerateCenterFrequencies(actual_frequencies,VB,VQT_MIN_FREQ,VQT_MAX_FREQ);
    const long double tau=2*acosl(-1.0L);
    for(int bin=0;bin<VB;++bin) {
        float f=(float)((long double)VQT_MIN_FREQ*exp2l((long double)bin/12));
        close_to(actual_frequencies[bin],f,0,0,"center frequency mismatch",bin);
        int length=(int)(q_policy(f)*44100/f);
        if(length>VN) length=VN; if(length<32) length=32;
        int start=(VN-length)/2, sparse=0;
        long double omega=tau*f/44100;
        long double complex phase=cexpl(I*omega*(start-VN/2));
        const VqtKernel* kernel=&vqtKernels[bin];
        assert(kernel->real&&kernel->imag&&kernel->indices&&kernel->length>0);
        for(int k=0;k<FN;++k) {
            long double theta=tau*k/VN;
            long double complex value=cexpl(-I*theta*start)/(2*length)
                *(phase*window_transform(theta-omega,length)
                  +conjl(phase)*window_transform(theta+omega,length));
            bool retained=cabsl(value)>0.02L;
            bool found=sparse<kernel->length&&kernel->indices[sparse]==k;
            if(retained!=found) {
                fprintf(stderr,"kernel sparsity mismatch bin=%d fft_bin=%d magnitude=%.17Lg retained=%d found=%d\n",bin,k,cabsl(value),retained,found);
                abort();
            }
            if(found) {
                close_to(kernel->real[sparse],creall(value),0.000003,0.0001,"kernel real mismatch",bin);
                close_to(kernel->imag[sparse],cimagl(value),0.000003,0.0001,"kernel imaginary mismatch",bin);
                ++sparse; ++sparse_coefficients;
            }
            ++coefficients;
        }
        assert(sparse==kernel->length);
    }
}

/* Independently implemented radix-two complex transform in long double.
 * It is validated below against direct sums and an analytic impulse. */
static void reference_fft(const float* mono)
{
    for(unsigned i=0,j=0;i<VN;++i) {
        spectrum[j]=mono[i];
        unsigned bit=VN>>1; while(j&bit) { j^=bit; bit>>=1; } j^=bit;
    }
    const long double tau=2*acosl(-1.0L);
    for(int width=2;width<=VN;width*=2) {
        long double complex step=cexpl(-I*tau/width);
        for(int base=0;base<VN;base+=width) {
            long double complex weight=1;
            for(int j=0;j<width/2;++j) {
                long double complex a=spectrum[base+j], b=weight*spectrum[base+j+width/2];
                spectrum[base+j]=a+b; spectrum[base+j+width/2]=a-b; weight*=step;
            }
        }
    }
    const int probes[]={0,1,7,31,128,511,2047,4096};
    long double input_norm=0;
    for(int j=0;j<VN;++j) input_norm+=fabsl(mono[j]);
    for(unsigned at=0;at<sizeof probes/sizeof *probes;++at) {
        int k=probes[at]; long double complex value=0, weight=1, step=cexpl(-I*tau*k/VN);
        for(int j=0;j<VN;++j) { value+=mono[j]*weight; weight*=step; }
        /* Direct DFT uses VN recurrent rotations and accumulations; cancellation
         * makes an output-relative-only tolerance invalid. Bound their forward
         * rounding error by input L1 norm, operation count and actual precision.
         * The factor 32 covers complex products/sums and the radix-two path. */
        long double tolerance=32*VN*LDBL_EPSILON*input_norm+1e-12L;
        if(cabsl(value-spectrum[k])>=tolerance) {
            fprintf(stderr,"oracle DFT mismatch frame=%u bin=%d error=%.17Lg bound=%.17Lg input_l1=%.17Lg precision=%d\n",frames,k,cabsl(value-spectrum[k]),tolerance,input_norm,LDBL_MANT_DIG);
            abort();
        }
    }
    long double time_energy=0,frequency_energy=0;
    for(int i=0;i<VN;++i) { time_energy+=(long double)mono[i]*mono[i]; frequency_energy+=creall(spectrum[i])*creall(spectrum[i])+cimagl(spectrum[i])*cimagl(spectrum[i]); }
    assert(fabsl(time_energy-frequency_energy/VN)<1e-10L+time_energy*1e-12L);
}
static void oracle_self_test(void)
{
    float impulse[VN]={0}; impulse[123]=1;
    reference_fft(impulse);
    const long double tau=2*acosl(-1.0L);
    for(int k=0;k<VN;++k) assert(cabsl(spectrum[k]-cexpl(-I*tau*k*123/VN))<1e-12L);
}
static void update_reference(const float* mono)
{
    reference_fft(mono);
    for(int bin=0;bin<VB;++bin) {
        /* Coefficients are first qualified independently by check_kernels.
         * Treat their float values as matrix inputs to isolate DSP rounding. */
        const VqtKernel* kernel=&vqtKernels[bin]; long double complex value=0;
        for(int at=0;at<kernel->length;++at)
            value+=spectrum[kernel->indices[at]]*((long double)kernel->real[at]+I*(long double)kernel->imag[at]);
        raw[bin]=(double)(2*cabsl(value));
    }
    double log_prefix[VB+1]={0};
    for(int i=0;i<VB;++i) log_prefix[i+1]=log_prefix[i]+log(raw[i]+(double)VQT_WHITENING_EPS);
    for(int i=0;i<VB;++i) {
#if VQT_SPECTRAL_WHITENING_ENABLED
        int half=VQT_WHITENING_WIDTH_BINS/2;
        int begin=i-half<0?0:i-half, end=i+half>=VB?VB-1:i+half;
        double environment=(log_prefix[end+1]-log_prefix[begin])/(end-begin+1);
        double contrast=fmax(0,exp(log(raw[i]+(double)VQT_WHITENING_EPS)-environment)-1);
        white[i]=raw[i]==0?0:(1-(double)VQT_WHITENING_STRENGTH)*raw[i]+(double)VQT_WHITENING_STRENGTH*contrast;
#else
        white[i]=raw[i];
#endif
        smooth[i]=(double)VQT_SMOOTHING_FACTOR*smooth[i]+(1-(double)VQT_SMOOTHING_FACTOR)*raw[i];
        white_smooth[i]=(double)VQT_SMOOTHING_FACTOR*white_smooth[i]+(1-(double)VQT_SMOOTHING_FACTOR)*white[i];
    }
    double current=0,current_white=0;
    for(int i=0;i<VB;++i) { current=fmax(current,smooth[i]); current_white=fmax(current_white,white_smooth[i]); }
    peak=fmax(0.0001,current>peak?current:peak*0.99+current*0.01);
    white_peak=fmax(0.0001,current_white>white_peak?current_white:white_peak*0.99+current_white*0.01);
}
static void check_values(void)
{
    typedef double (*api)(tic_mem*,s32);
    const api functions[]={tic_api_vqt,tic_api_vqts,tic_api_vqtr,tic_api_vqtrs,tic_api_vqtw,tic_api_vqtsw,tic_api_vqtrw,tic_api_vqtrsw};
    for(int i=0;i<VB;++i) {
        double expected[]={raw[i]/peak,fmin(1,smooth[i]/peak),raw[i],smooth[i],white[i]/white_peak,fmin(1,white_smooth[i]/white_peak),white[i],white_smooth[i]};
        max_raw_error=fmax(max_raw_error,fabs(vqtData[i]-raw[i]));
        max_white_error=fmax(max_white_error,fabs(vqtWhiteData[i]-white[i]));
        for(int at=0;at<8;++at) {
            close_to(functions[at](NULL,i),expected[at],at==2||at==3?0.0003:0.0001,0.001,"VQT API mismatch",i);
            ++api_queries;
        }
        assert(vqtNormalizedData[i]>=0&&vqtNormalizedData[i]<=1);
        assert(vqtWhiteNormalizedData[i]>=0&&vqtWhiteNormalizedData[i]<=1);
    }
    const int invalid[]={INT_MIN,-1,VB,INT_MAX};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;++i) for(int at=0;at<8;++at) {
        assert(functions[at](NULL,invalid[i])==0); ++api_queries;
    }
    close_to(vqtPeakSmoothValue,peak,0.0003,0.001,"VQT peak mismatch",0);
    close_to(vqtWhitePeakSmoothValue,white_peak,0.0003,0.001,"VQT white peak mismatch",0);
}
static void audio_frames(void)
{
    float stereo[VN*2],mono[VN]; unsigned random=0xb013cf97;
    const double tau=2*acos(-1.0);
    for(int signal=0;signal<10;++signal) {
        for(int j=0;j<VN;++j) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            double noise=((double)(random&0xffff)/65535-.5)*0.02;
            double value=noise;
            switch(signal) {
            case 0: value=0; break;
            case 1: value+=0.25*cos(tau*440*j/44100); break;
            case 2: value+=0.0025*cos(tau*440*j/44100); break;
            case 3: value+=0.5*sin(tau*880*j/44100); break;
            case 4: value+=0.2*cos(tau*19.445*j/44100)+0.1*sin(tau*18800*j/44100); break;
            case 5: value+=0.1*cos(tau*147.37*j/44100)+0.2*sin(tau*4000.25*j/44100); break;
            case 6: value=j==123?1:0; break;
            case 7: value=0.125; break;
            case 8: value=(noise+0.25*cos(tau*440*j/44100))*1e20; break;
            case 9: value+=0.25*cos(tau*440*j/44100); break;
            }
            stereo[2*j]=(float)value; stereo[2*j+1]=(float)value; mono[j]=(float)value;
        }
        feed(stereo,VN); update_reference(mono); VQT_ProcessAudio(); check_values(); ++frames;
    }
}
#ifndef TM_VQT_SPECTRUM_NO_MAIN
int main(void)
{
    oracle_self_test(); assert(FFT_Open(false,NULL)); assert(vqtEnabled);
    check_kernels(); audio_frames(); FFT_Close(); clean();
    printf("VQT spectrum oracle passed: frames=%u bins=%d APIs=8 queries=%u kernel_bins=%u retained=%u whitening=%d max_raw_error=%.9g max_white_error=%.9g\n",frames,VB,api_queries,coefficients,sparse_coefficients,VQT_SPECTRAL_WHITENING_ENABLED,max_raw_error,max_white_error);
    return 0;
}
#endif
