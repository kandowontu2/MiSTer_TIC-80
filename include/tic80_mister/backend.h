#ifndef TIC80_MISTER_BACKEND_H
#define TIC80_MISTER_BACKEND_H
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include "tic80_mister/exchange.h"
#include "tic80_mister/pacer.h"
typedef struct {
    uint32_t keys[16]; /* PS/2 set-2 physical codes; extended codes add 256. */
    uint32_t mouse;    /* x, y, then PS/2 left/right/middle button bits. */
    uint32_t mouse_gate; /* OSD epoch bits 11:0, open bit 12, capability bit 13; zero on old FPGA. */
    uint32_t wheel;    /* wrapping signed vertical-wheel total */
    uint32_t horizontal_wheel; /* wrapping signed total, positive right */
    uint32_t joystick[4]; /* raw MiSTer buttons, including remapped keyboard actions */
} tm_input_snapshot;
typedef struct {
    uint64_t device, inode;
    int64_t modified_seconds, changed_seconds;
    uint32_t modified_nanos, changed_nanos;
} tm_core_generation;
typedef struct {
    volatile uint8_t *memory;
    int fd;
    const char *core_name_path; /* NULL for DDR fixtures; /tmp/CORENAME on hardware. */
    uint32_t session;
    uint32_t audio_write;
    uint32_t cart_seen;
    tm_input_snapshot inputs;
    tm_exchange exchange;
    tm_pacer pacer;
    uint64_t video_wait_started_ms;
    uint8_t *deferred_frame;
    int deferred_valid;
    struct tm_linux_wheel *linux_wheel;
    int physical_input;
} tm_backend;
/* NULL selects /dev/mem on ARM. A regular file is allowed for transport tests.
 * No payload/control writes occur before identity and a moving heartbeat pass. */
int tm_backend_open(tm_backend *backend, const char *test_file);
int tm_backend_start(tm_backend *backend);
int tm_backend_restart(tm_backend *backend);
/* 1 = TIC-80 selected (or unrestricted fixture), 0 = another named core;
 * -1 = missing/empty/malformed selection. Missing/empty rewrites are retried
 * for at most 50 ms without DDR access. */
int tm_backend_core_selected(const tm_backend *backend);
/* Main rewrites CORENAME after releasing initialization reset. Its file
 * generation distinguishes a ready reload from an early fresh FPGA session.
 * 1 = matching selection and generation, 0 = unrestricted fixture, -1 = lost
 * selection or unavailable metadata. This does not read or write DDR. */
int tm_backend_core_generation(const tm_backend *backend, tm_core_generation *generation);
int tm_backend_present(tm_backend *backend, const uint8_t *rgba, size_t size);
/* Streaming service: preserve the pending frame; retain the latest complete
 * picture in RAM and publish it during pacing once its predecessor is ready. */
int tm_backend_present_realtime(tm_backend *backend, const uint8_t *rgba, size_t size);
int tm_backend_drain(tm_backend *backend);
/* Stereo S16 native little endian, with an even frame count (TIC-80 emits 800). */
int tm_backend_audio(tm_backend *backend, const int16_t *stereo, unsigned frames);
/* After one 800-frame TIC output, wait for the next playback-driven tick.
 * Maintain two ticks of audio reserve once playback starts. Startup uses a
 * 60 Hz wall-clock deadline until then. An optional
 * signal flag interrupts pacing so shutdown does not wait on a stalled DAC. */
int tm_backend_pace(tm_backend *backend, const volatile sig_atomic_t *cancel);
/* Bounded product wait derived from current PCM occupancy. Keep reserve
 * frames available for the subsequent publication; -1 means lost transport. */
int tm_backend_work_budget(tm_backend *backend, unsigned reserve, unsigned maximum_ms);
uint32_t tm_backend_gamepads(tm_backend *backend);
uint32_t tm_backend_status(tm_backend *backend);
/* 1 = coherent new/current snapshot; 0 = cached snapshot during an update;
 * -1 = lost session. Does not acknowledge or consume FPGA event state. */
int tm_backend_inputs(tm_backend *backend, tm_input_snapshot *snapshot);
int tm_backend_cart_pending(tm_backend *backend);
/* 1 = private cartridge copy; 0 = no new ticket; 2 = rejected transfer;
 * -1 = transport/allocation error. The caller owns *bytes on success. */
int tm_backend_cart(tm_backend *backend, uint8_t **bytes, size_t *size);
/* Copies an optional source path before ACK, owned by this same cart ticket.
 * Unknown/old bridge capabilities and malformed tags produce an empty path.
 * Valid output pointers are cleared on no-ticket/rejection/error returns.
 * Both accepted and rejected tickets are revalidated before staging ACK.
 * The worker must still verify the file bytes before using it for Save. */
int tm_backend_cart_source(tm_backend *backend, uint8_t **bytes, size_t *size, char source[256]);
void tm_backend_close(tm_backend *backend);
#endif
