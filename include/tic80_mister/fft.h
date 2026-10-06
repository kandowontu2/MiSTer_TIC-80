#ifndef TIC80_MISTER_FFT_H
#define TIC80_MISTER_FFT_H
#include <stdbool.h>
#include <stdint.h>
#define TM_FFT_DEVICE_BYTES 256
typedef struct {
    uint32_t enabled;
    char device[TM_FFT_DEVICE_BYTES];
} tm_fft_config;
enum { TM_FFT_DISABLED, TM_FFT_ACTIVE, TM_FFT_UNAVAILABLE, TM_FFT_PAUSED };
/* NULL device selects the default input; a named device must match capture
 * enumeration. Failed validation leaves the caller's configuration intact. */
int tm_fft_configure(tm_fft_config *config, const char *device);
bool tm_fft_config_valid(const tm_fft_config *config);
bool tm_fft_status_valid(const tm_fft_config *config, int status);
bool tm_fft_supported(void);
const char *tm_fft_status_name(int status);
/* Worker-owned lifecycle: the supervisor copies configuration but never opens
 * audio devices. Unavailable capture leaves the cartridge/editor usable, with
 * an explicit startup status and diagnostic. Close before releasing the worker. */
int tm_fft_worker_start(const tm_fft_config *config);
void tm_fft_worker_close(void);
#endif
