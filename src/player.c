#define _POSIX_C_SOURCE 200809L
#include "tic80.h"
#include "tic80_mister/video.h"
#include "tic80_mister/cart.h"
#include "tic80_mister/cart_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static int failed, requested_exit;
static void on_error(const char *message) { fprintf(stderr, "TIC-80: %s\n", message); failed = 1; }
static void on_trace(const char *message, u8 color) { (void)color; fprintf(stderr, "%s\n", message); }
static void on_exit(void) { requested_exit = 1; }
static u64 counter(void *data)
{
    (void)data;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u64)now.tv_sec * 1000000000ULL + now.tv_nsec;
}
static u64 logical_tick;
static u64 tick_counter(void *data) { (void)data; return logical_tick; }
static u64 frequency(void *data) { (void)data; return 60; }

/* Offline execution harness: output a final RGBA8888 frame and interleaved PCM.
 * Frame scheduling and live DDR transport are added separately for MiSTer. */
int main(int argc, char **argv)
{
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s cartridge.tic ticks frame.rgba audio.s16le [tick1_button_mask]\n", argv[0]);
        return 2;
    }
    char *end;
    long ticks = strtol(argv[2], &end, 10);
    if (!*argv[2] || *end || ticks < 1 || ticks > 36000) return 2;
    u32 buttons = 0;
    if (argc == 6) {
        errno = 0;
        unsigned long parsed = strtoul(argv[5], &end, 0);
        if (errno || !*argv[5] || *end || parsed > UINT32_MAX || argv[5][0] == '-') return 2;
        buttons = (u32)parsed;
    }
    FILE *cart = fopen(argv[1], "rb");
    if (!cart) { perror(argv[1]); return 1; }
    if (fseek(cart, 0, SEEK_END)) { fclose(cart); return 1; }
    long size = ftell(cart);
    if (size < 4 || size > 4 * 1024 * 1024) { fclose(cart); return 1; }
    rewind(cart);
    u8 *bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, cart) != (size_t)size) {
        free(bytes); fclose(cart); return 1;
    }
    fclose(cart);
    uint8_t *native=NULL; size_t native_size=0;
    if (tm_cart_file_decode(bytes,(size_t)size,&native,&native_size)) {
        fprintf(stderr, "Invalid or unsupported TIC-80 cartridge\n");
        free(bytes); return 1;
    }
    tic80 *tic = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    tic->callback.error = on_error;
    tic->callback.trace = on_trace;
    tic->callback.exit = on_exit;
    tic80_load(tic,native,(s32)native_size);
    free(native); free(bytes);
    FILE *audio = fopen(argv[4], "wb");
    if (!audio) { perror(argv[4]); tic80_delete(tic); return 1; }
    tic80_input input = {0};
    u64 start = counter(NULL);
    long executed = 0;
    uint8_t frame[TM_FRAME_BYTES];
    for (; executed < ticks && !failed && !requested_exit; ++executed) {
        logical_tick = (u64)executed;
        input.gamepads.data = executed == 1 ? buttons : 0;
        tic80_tick(tic, input, tick_counter, frequency);
        tic80_sound(tic);
        if (tm_video_convert(frame, sizeof frame, (const uint8_t *)tic->screen,
                             TIC80_FULLWIDTH * TIC80_FULLHEIGHT * 4, TIC80_FULLWIDTH * 4)) failed = 1;
        /* samples.count is the count of individual S16 samples, including L/R. */
        if (fwrite(tic->samples.buffer, sizeof(s16), tic->samples.count, audio) != (size_t)tic->samples.count) {
            perror("audio write"); failed = 1;
        }
    }
    double elapsed = (counter(NULL) - start) / 1000000000.0;
    if (fclose(audio)) failed = 1;
    FILE *video = fopen(argv[3], "wb");
    if (!video) { perror(argv[3]); failed = 1; }
    else {
        if (fwrite(frame, 1, sizeof frame, video) != sizeof frame) failed = 1;
        if (fclose(video)) failed = 1;
    }
    printf("ticks=%ld wall_seconds=%.6f average_tick_ms=%.3f\n",
           executed, elapsed, executed ? elapsed * 1000 / executed : 0);
    tic80_delete(tic);
    return failed ? 1 : 0;
}
