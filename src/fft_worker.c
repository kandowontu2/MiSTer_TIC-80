#include "tic80_mister/fft.h"
#include "ext/fft.h"
#include <stdio.h>
#include <string.h>

int tm_fft_configure(tm_fft_config *config, const char *device)
{
    if(!config || (device && (!*device || strlen(device)>=TM_FFT_DEVICE_BYTES))) return -1;
    tm_fft_config candidate={.enabled=1};
    if(device) strcpy(candidate.device,device);
    *config=candidate; return 0;
}
bool tm_fft_config_valid(const tm_fft_config *config)
{
    return config && config->enabled<=1 && memchr(config->device,0,sizeof config->device) &&
        (config->enabled || !*config->device);
}
bool tm_fft_status_valid(const tm_fft_config *config,int status)
{
    return tm_fft_config_valid(config) && (config->enabled
        ? (status==TM_FFT_ACTIVE || status==TM_FFT_UNAVAILABLE || status==TM_FFT_PAUSED)
        : status==TM_FFT_DISABLED);
}
bool tm_fft_supported(void)
{
#ifdef TIC80_FFT_UNSUPPORTED
    return false;
#else
    return true;
#endif
}
const char *tm_fft_status_name(int status)
{
    switch(status) {
    case TM_FFT_DISABLED: return "disabled";
    case TM_FFT_ACTIVE: return "active";
    case TM_FFT_UNAVAILABLE: return "unavailable";
    case TM_FFT_PAUSED: return "paused";
    default: return "invalid";
    }
}
int tm_fft_worker_start(const tm_fft_config *config)
{
    tm_fft_worker_close();
    if(!tm_fft_config_valid(config)) return TM_FFT_UNAVAILABLE;
    if(!config->enabled) return TM_FFT_DISABLED;
#ifndef TIC80_FFT_UNSUPPORTED
    if(FFT_Open(false,*config->device?config->device:NULL)) return TM_FFT_ACTIVE;
#endif
    fprintf(stderr,"TIC-80 microphone capture unavailable; FFT returns zero\n");
    return TM_FFT_UNAVAILABLE;
}
void tm_fft_worker_close(void) { FFT_Close(); }
