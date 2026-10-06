#define _POSIX_C_SOURCE 200809L
#include "ext/fft.h"
#include "fftdata.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Standalone diagnostic; worker frontends enable capture only when requested. */
int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--list")) { FFT_EnumerateDevices(); return 0; }
    if (argc != 3 || strcmp(argv[1], "--capture"))
    { fprintf(stderr, "Usage: %s --list | --capture device-name-substring\n", argv[0]); return 2; }
    g_currentLogLevel = FFT_LOG_WARNING;
    if (!FFT_Open(false, argv[2]))
    { fprintf(stderr, "Capture unavailable for requested ALSA device\n"); return 1; }
    struct timespec pause = {.tv_nsec = 16666667};
    for (int frame = 0; frame < 120; ++frame)
    {
        nanosleep(&pause, NULL);
        FFT_GetFFT(fftData);
        if (!(frame % 30))
        {
            int peak = 0;
            for (int i = 1; i < FFT_SIZE; ++i) if (fftRawData[i] > fftRawData[peak]) peak = i;
            printf("frame=%d peak_bin=%d frequency_hz=%.2f raw=%.6f\n", frame, peak,
                peak * 44100.0 / (FFT_SIZE * 2), fftRawData[peak]);
        }
    }
    FFT_Close();
    return 0;
}
