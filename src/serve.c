#define _GNU_SOURCE
#include "tic80.h"
#include "tic80_mister/backend.h"
#include "tic80_mister/cart.h"
#include "tic80_mister/pmem.h"
#include "tic80_mister/video.h"
#include "tic80_mister/input.h"
#include "tic80_mister/vm.h"
#include "tic80_mister/memory_map.h"
#include "tic80_mister/main_launch.h"
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/resource.h>
static volatile sig_atomic_t stopping;
static int script_error, cart_exit;
static uint64_t time_offset;
static void interrupt(int sig) { (void)sig; stopping = 1; }
static void error(const char *s) { fprintf(stderr, "TIC-80: %s\n", s); script_error = 1; }
static void trace(const char *s, u8 color) { (void)color; fprintf(stderr, "%s\n", s); }
static void exit_cart(void) { cart_exit = 1; }
static u64 real_counter(void *data)
{
    (void)data;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static u64 frequency(void *data) { (void)data; return 1000000000ULL; }
static int restart_transport(tm_backend *backend, int *lost_session)
{
    // Main can hold/reset the bridge longer than a single handshake. A new
    // FPGA also captures the first stale DDR nonce and requires a later one.
    // Withhold ticks while retrying; a different selected core ends the wait.
    u64 until = real_counter(NULL) + 10000000000ULL;
    while (!stopping && real_counter(NULL) < until) {
        int selected = tm_backend_core_selected(backend);
        if (selected != 1) return -1;
        if (!tm_backend_restart(backend)) return 0;
        if (lost_session) *lost_session = 1;
        struct timespec pause = {0, 5000000};
        nanosleep(&pause, NULL);
    }
    return -1;
}
#ifdef TM_TRACE_RESETS
static void reset_trace(tm_backend *b, const char *event, uint32_t status,
                        uint32_t previous, unsigned long ticks)
{
    struct stat name = {0};
    if (b->core_name_path) stat(b->core_name_path, &name);
    uint32_t ack = *(volatile uint32_t *)(b->memory + TM_SESSION_ACK_OFFSET);
    uint32_t request = *(volatile uint32_t *)(b->memory + TM_SESSION_REQUEST_OFFSET);
    uint32_t identity = *(volatile uint32_t *)(b->memory + TM_IDENTITY_OFFSET);
    fprintf(stderr, "Reset trace: ns=%llu event=%s status=%08x previous=%08x session=%u ack=%u request=%u identity=%08x ticks=%lu name=%lld.%09ld\n",
        (unsigned long long)real_counter(NULL), event, status, previous,
        b->session, ack, request, identity, ticks, (long long)name.st_mtim.tv_sec, name.st_mtim.tv_nsec);
}
#else
#define reset_trace(...) ((void)0)
#endif
static tic80 *new_runtime(const uint8_t *bytes, size_t size)
{
    tic80 *tic = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    if (!tic) return NULL;
    tic->callback.error = error;
    tic->callback.trace = trace;
    tic->callback.exit = exit_cart;
    tic80_load(tic, (void *)bytes, (s32)size);
    return tic;
}
static tic80 *screen(const char *heading, const char *line)
{
    /* All strings here are program-owned literals. Cartridge text is logged,
     * never interpolated into the splash cartridge's executable Lua source. */
    char code[1024];
    int length = snprintf(code, sizeof code,
        "-- script: lua\nfunction TIC() cls(0); rect(8,12,224,112,1); "
        "print('%s',24,30,12,false,2); print('%s',24,64,12); "
        "print('Win+F12: Load Cartridge',24,84,11) end\n", heading, line);
    if (length < 1 || (size_t)length >= sizeof code) return NULL;
    uint8_t bytes[1032] = {17, 0, 0, 0, 5, 0, 0, 0};
    bytes[5] = (uint8_t)length;
    bytes[6] = (uint8_t)(length >> 8);
    memcpy(bytes + 8, code, (size_t)length);
    return new_runtime(bytes, (size_t)length + 8);
}
static int video(tm_backend *b, tic80 *tic)
{
    uint8_t frame[TM_FRAME_BYTES];
    if (tm_video_convert(frame, sizeof frame, (uint8_t *)tic->screen,
            TIC80_FULLWIDTH * TIC80_FULLHEIGHT * 4, TIC80_FULLWIDTH * 4)) return -2;
    return tm_backend_present_realtime(b, frame, sizeof frame);
}
static int loading(tm_backend *b)
{
    tic80 *ui = screen("Loading...", "Please wait");
    if (!ui) return -2;
    tic80_input input = {0};
    tic80_tick(ui, input, real_counter, frequency);
    int result = video(b, ui);
    if (!result) result = tm_backend_drain(b);
    tic80_delete(ui);
    return result;
}
static void tick_game(tm_vm *vm, tic80_input input)
{
    int result = tm_vm_tick(vm, input, time_offset);
    if (result == TM_VM_EXIT) cart_exit = 1;
    else if (result != TM_VM_OK) {
        script_error = 1;
        if (result == TM_VM_TIMEOUT) fprintf(stderr, "Cartridge execution timed out\n");
        else if (result == TM_VM_DIED) fprintf(stderr, "Cartridge execution process stopped\n");
    }
}
static tm_vm *load_game(const uint8_t *bytes, size_t size, const char *directory, tm_pmem *save,const tm_fft_config *capture)
{
    tm_vm *vm = NULL;
    int result = tm_vm_open_configured(&vm, bytes, size,capture);
    if (result != TM_VM_OK) {
        if (result == TM_VM_TIMEOUT) fprintf(stderr, "Cartridge initialization timed out\n");
        else if (result == TM_VM_DIED) fprintf(stderr, "Cartridge initialization process stopped\n");
        return NULL;
    }
    if(capture->enabled) fprintf(stderr,"Cartridge microphone capture: %s\n",tm_fft_status_name(tm_vm_fft_status(vm)));
    tic80 *tic = tm_vm_product(vm);
    char *path = malloc(strlen(directory) + 40);
    if (!path) { tm_vm_close(vm); return NULL; }
    sprintf(path, "%s/%s.pmem", directory, tm_vm_key(vm));
    int loaded = tm_pmem_open(save, tic, path);
    free(path);
    if (loaded) { tm_vm_close(vm); return NULL; }
    return vm;
}
int tm_serve(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s --serve save_directory [--fft | --fft-device name] [--memory fixture [--core-name fixture]] [--ticks N] [--pulse mask]\n", argv[0]);
        return 2;
    }
    const char *memory_file = NULL, *core_name_file = NULL, *main_processes = NULL;
    unsigned long limit = 0, pulse = 0;
    tm_fft_config capture={0};
    for (int i = 3; i < argc; ++i) {
        if(!strcmp(argv[i],"--fft")) { if(tm_fft_configure(&capture,NULL)) return 2; continue; }
        if (i + 1 >= argc) return 2;
        if (!strcmp(argv[i], "--memory")) memory_file = argv[i+1];
        else if (!strcmp(argv[i], "--core-name")) core_name_file = argv[i+1];
        else if (!strcmp(argv[i], "--main-processes")) main_processes = argv[i+1];
        else if (!strcmp(argv[i], "--fft-device")) { if(tm_fft_configure(&capture,argv[i+1])) return 2; }
        else {
            char *end;
            errno = 0;
            unsigned long n = strtoul(argv[i+1], &end, 0);
            if (errno || !*argv[i+1] || *end || argv[i+1][0] == '-') return 2;
            if (!strcmp(argv[i], "--ticks")) limit = n;
            else if (!strcmp(argv[i], "--pulse") && n <= UINT32_MAX) pulse = n;
            else return 2;
        }
        ++i;
    }
    // Selection injection is a fixture facility, never a physical-core override.
    if ((core_name_file || main_processes) && !memory_file) return 2;
    if (!memory_file) {
        /* MiSTer's Main runs on CPU 1. Keep the player and the interpreter it
         * spawns on CPU 0 so screenshot compression cannot compete with the
         * next audio tick. File-backed host fixtures retain their affinity. */
        cpu_set_t allowed, playback;
        if (sched_getaffinity(0, sizeof allowed, &allowed) || !CPU_ISSET(0, &allowed)) {
            fprintf(stderr, "Playback CPU 0 is unavailable\n");
            return 1;
        }
        CPU_ZERO(&playback);
        CPU_SET(0, &playback);
        if (sched_setaffinity(0, sizeof playback, &playback)) {
            perror("Playback CPU affinity");
            return 1;
        }
        /* Transfers and checksum processes otherwise compete with each frame's
         * audio deadline on CPU 0. The exec worker inherits this CFS priority;
         * the watchdog stays schedulable even if a cartridge loops forever. */
        if (setpriority(PRIO_PROCESS, 0, -10)) {
            perror("Playback scheduling priority");
            return 1;
        }
    }
    signal(SIGINT, interrupt);
    signal(SIGTERM, interrupt);
    tm_backend backend;
    if (tm_backend_open(&backend, memory_file)) return 1;
    if (core_name_file) backend.core_name_path = core_name_file;
    if (tm_backend_start(&backend)) { tm_backend_close(&backend); return 1; }
    tic80 *tic = screen("TIC-80", "Choose a cartridge in the menu");
    tic80 *notice = NULL;
    tic80 *reset_screen = NULL;
    tm_vm *game = NULL;
    tm_pmem save = {0};
    tm_input_state input_state = {0};
    uint8_t *cached = NULL;
    size_t cached_size = 0;
    int playing = 0, failed = !tic, force_reset = 0, first_frame = 0;
    unsigned transport_failures = 0;
    int transport_failed = 0, departed = 0;
    uint32_t previous_status = tm_backend_status(&backend);
    tm_core_generation generation = {0};
    int generation_valid = tm_backend_core_generation(&backend, &generation) == 1;
    int reload_pending = 0;
    int mgl_wait = 0;
    u64 reload_started = 0;
    u64 pause_started = 0, notice_until = 0;
    unsigned long ticks = 0, game_ticks = 0;
    puts("TIC-80 service ready; waiting for OSD cartridge transfers"); fflush(stdout);
    while (!stopping && !failed && (!limit || ticks < limit)) {
        int selected = tm_backend_core_selected(&backend);
        if (selected == 0) { departed = 1; break; }
        if (selected < 0) { reset_trace(&backend, "selection-unavailable", 0, previous_status, ticks); transport_failed = 1; break; }
        uint32_t status = tm_backend_status(&backend);
        int reset_released = !(status & 1) && (previous_status & 1);
        int initialized_now = 0;
        int pending = tm_backend_cart_pending(&backend);
        int reset_cart = force_reset || (!(status & 1) && (previous_status & 1) && cached);
        if (reset_cart) reset_trace(&backend, force_reset ? "forced-reset" : "reset-release", status, previous_status, ticks);
        force_reset = 0;
        previous_status = status;
        if (pending < 0) {
            reset_trace(&backend, "metadata-session-lost", status, previous_status, ticks);
            // Hardware reset preserves our private cartridge and saves. A
            // core switch loses identity and makes this restart fail safely.
            if (restart_transport(&backend, NULL)) { reset_trace(&backend, "metadata-restart-failed", status, previous_status, ticks); transport_failed = 1; break; }
            // A falling edge across two FPGA sessions belongs to the old
            // session, not to Main's new initialization. Establish new history.
            previous_status = status = tm_backend_status(&backend);
            reset_released = 0;
            reset_cart = cached != NULL;
            if (cached && backend.core_name_path) {
                if (!reload_pending) reload_started = real_counter(NULL);
                reload_pending = 1;
            }
            pending = tm_backend_cart_pending(&backend);
        }
        if (reload_pending) {
            tm_core_generation next = {0};
            int observed = tm_backend_core_generation(&backend, &next);
            if (observed < 0) { transport_failed = 1; break; }
            int initialized = reset_released || (observed == 1 && generation_valid &&
                memcmp(&generation, &next, sizeof generation));
            if (initialized) {
                reset_trace(&backend, "main-initialization-ready", status, previous_status, ticks);
                reload_pending = 0;
                reset_cart = cached != NULL;
                initialized_now = 1;
                if ((!memory_file || main_processes) && !pending) {
                    int launch = tm_main_initial_cart(main_processes);
                    mgl_wait = launch != 0;
                    if (mgl_wait) {
                        fprintf(stderr, "Waiting for initial MGL cartridge; launch_context=%d\n", launch);
                        if (!pause_started) pause_started = real_counter(NULL);
                        if (reset_screen) {
                            tic80_delete(reset_screen);
                            reset_screen = screen(launch < 0 ? "MGL file unavailable" : "Loading MGL cartridge",
                                                  "Load a cart or reset to cancel");
                            if (!reset_screen) { failed = 1; break; }
                        }
                    }
                }
            } else if (real_counter(NULL) - reload_started > 10000000000ULL) {
                fprintf(stderr, "MiSTer initialization did not complete\n");
                transport_failed = 1;
                break;
            }
        }
        // Main's initialization reset is released before its delayed MGL
        // action. Only an actual ticket or a subsequent user reset releases
        // this extra hold. A timeout cannot authorize cached cartridge BOOT.
        if (mgl_wait && (pending > 0 || (reset_released && !initialized_now))) mgl_wait = 0;
        // Main also asserts reset while replacing a core. Hold the cartridge
        // until release, so a departure cannot run an extra BOOT/save cycle.
        if ((status & 1) || reload_pending || mgl_wait) {
            pending = reset_cart = 0;
            if (!reset_screen) {
                reset_trace(&backend, "reset-hold-enter", status, previous_status, ticks);
                reset_screen = mgl_wait ? screen("Loading MGL cartridge", "Load a cart or reset to cancel") :
                    reload_pending ? screen("Reloading core", "Waiting for MiSTer initialization") :
                    screen("Reset held", "Release reset to restart");
                if (!reset_screen) { failed = 1; break; }
                if (restart_transport(&backend, NULL)) { reset_trace(&backend, "hold-restart-failed", status, previous_status, ticks); transport_failed = 1; break; }
            }
        } else if (reset_screen) {
            reset_trace(&backend, "reset-hold-leave", status, previous_status, ticks);
            tic80_delete(reset_screen);
            reset_screen = NULL;
        }
        if (pending || reset_cart) {
            reset_trace(&backend, pending ? "boot-new-cart" : "boot-cached-cart", status, previous_status, ticks);
            u64 paused_at = pause_started ? pause_started : real_counter(NULL);
            u64 old_offset = time_offset;
            if (notice) { tic80_delete(notice); notice = NULL; }
            pause_started = 0;
            uint8_t *bytes = cached;
            size_t size = cached_size;
            if (restart_transport(&backend, NULL)) { transport_failed = 1; break; }
            int loaded_screen = loading(&backend);
            if (loaded_screen) {
                if (loaded_screen == -2) failed = 1;
                else transport_failed = 1;
                break;
            }
            int saved = playing ? tm_pmem_save(&save, tic) : 0;
            int transfer = pending ? tm_backend_cart(&backend, &bytes, &size) : 1;
            tm_pmem next_save = {0};
            time_offset = 0;
            int capture_paused=0;
            if(game && capture.enabled && !saved && transfer==1) {
                if(tm_vm_fft_pause(game)!=TM_VM_OK) { failed=1; break; }
                capture_paused=1;
            }
            tm_vm *next = !saved && transfer == 1 ? load_game(bytes, size, argv[2], &next_save,&capture) : NULL;
            tm_input_state next_input = {0};
            if (next) {
                tic80_input initial = {0};
                initial.gamepads.data = tm_backend_gamepads(&backend);
                tm_input_snapshot snapshot;
                script_error = cart_exit = 0;
                if (tm_backend_inputs(&backend, &snapshot) < 0) script_error = 1;
                else {
                    tm_input_convert_player(&next_input, &snapshot, &initial);
                    tick_game(next, initial);
                }
                if (script_error || cart_exit) { tm_vm_close(next); next = NULL; tm_pmem_close(&next_save); }
            }
            if (next) {
                if (game) tm_vm_close(game);
                else if (tic) tic80_delete(tic);
                tm_pmem_close(&save);
                game = next;
                tic = tm_vm_product(game);
                save = next_save; // no worker exists until the first autosave
                if (pending) { free(cached); cached = bytes; cached_size = size; }
                playing = 1;
                input_state = next_input;
                game_ticks = 0;
                first_frame = 1; // present the already validated first tick
                printf("Cartridge %s: %zu bytes; reset=%d\n", pending ? "loaded" : "restarted", size, reset_cart); fflush(stdout);
            } else {
                if(capture_paused) {
                    if(tm_vm_fft_resume(game)!=TM_VM_OK) { failed=1; break; }
                    fprintf(stderr,"Previous cartridge microphone capture: %s\n",tm_fft_status_name(tm_vm_fft_status(game)));
                }
                fprintf(stderr, "Cartridge rejected: transfer=%d bytes=%zu save_error=%d script_error=%d exit=%d\n",
                        transfer, size, saved != 0, script_error, cart_exit);
                if (pending) free(bytes);
                time_offset = old_offset;
                notice = screen("Cannot load", saved ? "Save could not be written" : "Invalid cartridge or save file");
                if (!notice) { failed = 1; break; }
                pause_started = paused_at;
                notice_until = real_counter(NULL) + 2000000000ULL;
            }
            script_error = cart_exit = 0;
        }
        if (notice && real_counter(NULL) >= notice_until) {
            tic80_delete(notice); notice = NULL;
            if (pause_started) time_offset += real_counter(NULL) - pause_started;
            pause_started = 0;
            if (restart_transport(&backend, NULL)) { transport_failed = 1; break; }
        }
        tic80 *visible = reset_screen ? reset_screen : notice ? notice : tic;
        script_error = cart_exit = 0;
        if (playing && !notice && !reset_screen && first_frame) first_frame = 0;
        else {
            tic80_input input = {0};
            input.gamepads.data = tm_backend_gamepads(&backend);
            tm_input_snapshot snapshot;
            if (tm_backend_inputs(&backend, &snapshot) < 0) {
                reset_trace(&backend, "input-session-lost", status, previous_status, ticks);
                if (restart_transport(&backend, NULL)) { reset_trace(&backend, "input-restart-failed", status, previous_status, ticks); transport_failed = 1; break; }
                previous_status = tm_backend_status(&backend);
                force_reset = cached != NULL;
                if (cached && backend.core_name_path) {
                    if (!reload_pending) reload_started = real_counter(NULL);
                    reload_pending = 1;
                }
                continue;
            }
            tm_input_convert_player(&input_state, &snapshot, &input);
            if (playing && !notice && !reset_screen && game_ticks == 1) input.gamepads.data |= (uint32_t)pulse;
            if (playing && !notice && !reset_screen) tick_game(game, input);
            else tic80_tick(visible, input, real_counter, frequency);
        }
        // An interrupted worker tick leaves the last completed product intact.
        // Finish its save without restarting the VM or publishing more output.
        if (stopping) break;
        if (!playing || notice || reset_screen) tic80_sound(visible);
        if (!notice && !reset_screen && playing && (script_error || cart_exit)) {
            fprintf(stderr, "Cartridge %s\n", script_error ? "failed" : "requested exit");
            if (restart_transport(&backend, NULL)) { transport_failed = 1; break; }
            if (tm_pmem_save(&save, tic)) fprintf(stderr, "Could not finish cartridge save\n");
            tm_pmem_close(&save);
            tm_vm_close(game); game = NULL;
            tic = screen(script_error ? "Cartridge error" : "TIC-80", "Choose another cartridge");
            playing = 0;
            if (!tic) { failed = 1; break; }
            continue;
        }
        if (!notice && !reset_screen && playing) {
            ++game_ticks;
            if (game_ticks % 60 == 0 && tm_pmem_schedule(&save, tic)) {
                notice = screen("Save error", "Check the SD card");
                if (!notice) { failed = 1; break; }
                if (restart_transport(&backend, NULL)) { transport_failed = 1; break; }
                pause_started = real_counter(NULL);
                notice_until = pause_started + 2000000000ULL;
                continue;
            }
        }
        int output_result = tm_backend_audio(&backend, visible->samples.buffer, (unsigned)visible->samples.count / 2);
        if (!output_result) output_result = video(&backend, visible);
        if (!output_result) output_result = tm_backend_pace(&backend, &stopping);
        if (stopping) break;
        if (output_result == -2) { failed = 1; break; }
        if (output_result) {
            reset_trace(&backend, "output-session-lost", status, previous_status, ticks);
            // A bad output acknowledgment can occur while identity and the
            // session still match. Flush that transport without restarting the
            // VM or waiting for a Main initialization event. An actually lost
            // FPGA session still requires the normal reload/reset guard.
            int lost_session = tm_backend_cart_pending(&backend) < 0;
            if (++transport_failures >= 3 || restart_transport(&backend, &lost_session)) { reset_trace(&backend, "output-restart-failed", status, previous_status, ticks); transport_failed = 1; break; }
            previous_status = tm_backend_status(&backend);
            force_reset = lost_session && cached != NULL;
            if (lost_session && cached && backend.core_name_path) {
                if (!reload_pending) reload_started = real_counter(NULL);
                reload_pending = 1;
            }
            // A new transport also starts new wheel totals. Establish their
            // baseline before another tick, rather than replaying old deltas.
            input_state = (tm_input_state){0};
            continue;
        }
        if (!reload_pending) {
            tm_core_generation next = {0};
            int observed = tm_backend_core_generation(&backend, &next);
            if (observed == 1) { generation = next; generation_valid = 1; }
        }
        transport_failures = 0;
        ++ticks;
    }
    if (!stopping && !failed && !transport_failed && !departed && tm_backend_core_selected(&backend) != 0 && tm_backend_drain(&backend))
        transport_failed = 1;
    if (transport_failed && !stopping) {
        // An explicitly selected different core is an intentional exit. Missing
        // selection, lost identity on TIC-80, and save failures remain errors.
        if (tm_backend_core_selected(&backend) == 0) departed = 1;
        else {
            failed = 1;
            fprintf(stderr, tm_backend_core_selected(&backend) < 0 ?
                "Cannot determine selected MiSTer core\n" : "TIC-80 transport unavailable\n");
        }
    }
    if (playing && tm_pmem_save(&save, tic)) failed = 1;
    tm_pmem_close(&save);
    if (notice) tic80_delete(notice);
    if (reset_screen) tic80_delete(reset_screen);
    if (game) tm_vm_close(game);
    else if (tic) tic80_delete(tic);
    free(cached);
    tm_backend_close(&backend);
    if (departed) puts("Core switched; runtime stopped");
    printf("TIC-80 service stopped: ticks=%lu error=%d\n", ticks, failed); fflush(stdout);
    return failed ? 1 : 0;
}
