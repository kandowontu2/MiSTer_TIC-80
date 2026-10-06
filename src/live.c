#define _POSIX_C_SOURCE 200809L
#include "tic80.h"
#include "tic80_mister/backend.h"
#include "tic80_mister/hid_wheel.h"
#include "tic80_mister/cart.h"
#include "tic80_mister/cart_file.h"
#include "tic80_mister/video.h"
#include "tic80_mister/pmem.h"
#include "tic80_mister/input.h"
#include "tic80_mister/vm.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stop;
static void interrupt(int signal) { (void)signal; stop = 1; }
static u64 clock_ns(void *data)
{
    (void)data;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ULL + t.tv_nsec;
}
int tm_serve(int argc, char **argv);
int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--hid-wheel-worker")) return tm_hid_wheel_worker(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--vm-worker")) return tm_vm_worker(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--serve")) return tm_serve(argc, argv);
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: %s --probe | (--pattern|cartridge.tic) [ticks=600] [tick1_button_mask]\n", argv[0]);
        return 2;
    }
    char *end;
    errno = 0;
    long ticks = argc >= 3 ? strtol(argv[2], &end, 10) : 600;
    if (argc >= 3 && (errno || !*argv[2] || *end || ticks < 1 || ticks > 360000)) return 2;
    uint32_t pulse = 0;
    if (argc == 4) {
        errno = 0;
        unsigned long n = strtoul(argv[3], &end, 0);
        if (errno || !*argv[3] || *end || argv[3][0] == '-' || n > UINT32_MAX) return 2;
        pulse = (uint32_t)n;
    }
    signal(SIGINT, interrupt);
    signal(SIGTERM, interrupt);
    tm_backend backend;
    if (tm_backend_open(&backend, NULL)) return 1;
    if (!strcmp(argv[1], "--probe")) {
        puts("TIC-80 FPGA identity, geometry, moving heartbeat and reserved DDR verified");
        tm_backend_close(&backend);
        return 0;
    }
    tic80 *tic = NULL;
    tm_vm *game = NULL;
    tm_pmem save = {0};
    int pattern = !strcmp(argv[1], "--pattern");
    if (!pattern) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); goto fail; }
        if (fseek(f, 0, SEEK_END)) { fclose(f); goto fail; }
        long size = ftell(f);
        rewind(f);
        if (size < 4 || size > 4 * 1024 * 1024) { fclose(f); goto fail; }
        uint8_t *bytes = malloc((size_t)size);
        if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) {
            fclose(f); free(bytes); goto fail;
        }
        fclose(f);
        if (tm_cart_file_validate(bytes, (size_t)size)) { free(bytes); goto fail; }
        int created = tm_vm_open(&game, bytes, (size_t)size);
        free(bytes);
        if (created != TM_VM_OK) goto fail;
        tic = tm_vm_product(game);
        char *path = malloc(strlen(argv[1]) + 6);
        if (!path) goto fail;
        sprintf(path, "%s.pmem", argv[1]);
        int loaded = tm_pmem_open(&save, tic, path);
        free(path);
        if (loaded) goto fail;
    }
    if (tm_backend_start(&backend)) goto fail;
    uint8_t frame[TM_FRAME_BYTES];
    u64 started = clock_ns(NULL), start = started;
    long count = 0;
    tm_input_state input_state = {0};
    for (; count < ticks && !stop; ++count) {
        if (pattern) {
            for (unsigned y = 0; y < TM_HEIGHT; ++y) for (unsigned x = 0; x < TM_WIDTH; ++x) {
                uint8_t *pixel = frame + (y * TM_WIDTH + x) * 4;
                pixel[0] = (uint8_t)x; pixel[1] = (uint8_t)y;
                pixel[2] = (uint8_t)(count / 6); pixel[3] = 255;
                if (x == 0 || y == 0 || x == TM_WIDTH-1 || y == TM_HEIGHT-1)
                    pixel[0] = pixel[1] = pixel[2] = 255;
            }
        } else {
            tic80_input input = {0};
            input.gamepads.data = tm_backend_gamepads(&backend) | (count == 1 ? pulse : 0);
            tm_input_snapshot snapshot;
            if (tm_backend_inputs(&backend, &snapshot) < 0) goto fail;
            tm_input_convert_player(&input_state, &snapshot, &input);
            int result = tm_vm_tick(game, input, 0);
            if (result == TM_VM_EXIT) stop = 1;
            else if (result != TM_VM_OK) {
                fprintf(stderr, "Cartridge execution stopped: result=%d\n", result);
                goto fail;
            }
            if (count % 60 == 59 && tm_pmem_schedule(&save, tic)) goto fail;
            if (tm_backend_audio(&backend, tic->samples.buffer, (unsigned)tic->samples.count / 2)) goto fail;
            if (tm_video_convert(frame, sizeof frame, (uint8_t *)tic->screen,
                    TIC80_FULLWIDTH * TIC80_FULLHEIGHT * 4, TIC80_FULLWIDTH * 4)) goto fail;
        }
        if (tm_backend_present(&backend, frame, sizeof frame)) goto fail;
        if (!pattern) {
            if (tm_backend_pace(&backend, &stop)) goto fail;
            continue;
        }
        /* Transport acknowledgments also provide backpressure. Limit to 60 Hz
         * when the carrier is faster; never catch up by bursting old ticks. */
        u64 deadline = start + (u64)(count + 1) * 1000000000ULL / 60;
        u64 now = clock_ns(NULL);
        if (now < deadline) {
            struct timespec t = {(time_t)(deadline / 1000000000ULL), (long)(deadline % 1000000000ULL)};
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
        } else if (now - deadline > 16666667) start = now - (u64)(count + 1) * 1000000000ULL / 60;
    }
    if (tm_backend_drain(&backend)) goto fail;
    if (tic && tm_pmem_save(&save, tic)) goto fail;
    printf("Live frames acknowledged=%ld stereo_frames_played=%u wall_seconds=%.3f\n",
           count, backend.audio_write, (clock_ns(NULL)-started)/1e9);
    tm_vm_close(game);
    tm_pmem_close(&save);
    tm_backend_close(&backend);
    return 0;
fail:
    if (tic && save.path) tm_pmem_save(&save, tic);
    tm_vm_close(game);
    tm_pmem_close(&save);
    tm_backend_close(&backend);
    fprintf(stderr, "Live runtime stopped after a cartridge or transport error\n");
    return 1;
}
