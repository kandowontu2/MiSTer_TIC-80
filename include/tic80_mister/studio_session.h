#ifndef TIC80_MISTER_STUDIO_SESSION_H
#define TIC80_MISTER_STUDIO_SESSION_H
#include "tic80_mister/studio.h"
#include "tic80_mister/fft.h"
#include "studio/rom.h"
#include <sys/types.h>
typedef struct tm_studio_session tm_studio_session;
enum { TM_STUDIO_OK, TM_STUDIO_RECOVERED, TM_STUDIO_EXIT, TM_STUDIO_ERROR, TM_STUDIO_PENDING,
       TM_STUDIO_SAVE_ERROR, TM_STUDIO_CART_ERROR, TM_STUDIO_CONFIRM,
       TM_STUDIO_CART_SELECTED, TM_STUDIO_CANCELLED };
/* The calling executable dispatches --studio-worker to the worker entry point.
 * All Studio/interpreter work runs in a separate process group. */
int tm_studio_session_open(tm_studio_session **out, const char *folder);
/* Existing save directory, using the service's keyed TMPM format. RUN loads
 * in the worker; parent identity I/O prepares in a background thread. Only
 * accepted values are queued. Close completes pending storage work/flush. */
int tm_studio_session_open_saved(tm_studio_session **out, const char *folder, const char *save_directory);
int tm_studio_session_open_configured(tm_studio_session **out, const char *folder, const char *save_directory, const tm_fft_config *capture);
/* Pin each initial/replacement/cleanup worker independently of the frontend.
 * worker_cpu=-1 preserves the spawning thread's affinity. */
int tm_studio_session_open_on_cpu(tm_studio_session **out, const char *folder, const char *save_directory, const tm_fft_config *capture, int worker_cpu);
/* Last acknowledged capture startup status; recovery reopens the same copied
 * configuration in the replacement worker. Cleanup-only workers skip capture. */
int tm_studio_session_fft_status(const tm_studio_session *session);
int tm_studio_session_tick(tm_studio_session *session, tic80_input input, uint64_t clock_ns, unsigned timeout_ms);
/* One outstanding request. Poll returns PENDING while executing or restoring;
 * the last acknowledged getters remain valid throughout. wait_ms=0 never
 * waits for an ACK or storage preparation. An execution timeout bounds the
 * worker, not storage after its completed ACK. Close cancels an outstanding
 * request; unaccepted values are never queued, even during identity I/O.
 * Closing interrupted Save work reclaims its recorded private temporary in a
 * separate cleanup worker before releasing the transport. */
int tm_studio_session_begin_tick(tm_studio_session *session, tic80_input input, uint64_t clock_ns, unsigned timeout_ms);
/* Nonblocking load/RUN requests share poll and the one-outstanding rule.
 * Load copies the payload before returning; decoding/validation occurs in
 * the supervised worker. CART_ERROR preserves the previous cartridge. */
int tm_studio_session_begin_load(tm_studio_session *session, const u8 *data, size_t size, const char *name, unsigned timeout_ms);
/* Interactive OSD selection: validate before showing the upstream unsaved
 * changes dialog. CONFIRM keeps the old cart and requires normal input ticks;
 * CART_SELECTED authorizes RUN, CANCELLED retains edits. Reset/recovery cancels
 * the dialog; the frontend must retain its candidate to offer it again. */
int tm_studio_session_begin_select(tm_studio_session *session, const u8 *data, size_t size, const char *name, unsigned timeout_ms);
/* A transfer-bound source candidate is copied with the cart. The worker
 * verifies exact regular-file bytes before using its canonical name/path;
 * missing/mismatching/virtual sources fall back to the supplied working name. */
int tm_studio_session_begin_select_source(tm_studio_session *session, const u8 *data, size_t size, const char *name, const char *source, unsigned timeout_ms);
int tm_studio_session_begin_run(tm_studio_session *session, unsigned timeout_ms);
/* Cancel unaccepted work and restore the last ACK in its home editor. Poll
 * until RECOVERED before issuing another command; no BOOT/TIC is executed. */
int tm_studio_session_begin_pause(tm_studio_session *session);
int tm_studio_session_poll(tm_studio_session *session, unsigned wait_ms);
int tm_studio_session_load(tm_studio_session *session, const u8 *data, size_t size, const char *name);
/* Explicit local file load; bounded worker I/O, validated native/PNG data.
 * A rejected file keeps the current cart and its Save destination. */
int tm_studio_session_load_file(tm_studio_session *session, const char *path, unsigned timeout_ms);
/* Enter RUN and complete its first tick without depending on console startup
 * animation or an injected keyboard shortcut being accepted. SAVE_ERROR
 * rejects RUN before BOOT/TIC while keeping the acknowledged editor usable;
 * the save file is preserved and a later RUN can retry its load. */
int tm_studio_session_run(tm_studio_session *session, unsigned timeout_ms);
int tm_studio_session_clipboard(tm_studio_session *session, const char *text);
int tm_studio_session_close(tm_studio_session *session);
int tm_studio_session_worker(int argc, char **argv);
/* Last acknowledged state: never expose partially completed worker updates.
 * A replacement worker can acknowledge a recovered Save destination after
 * verifying its prepared inode/content against this same cart checkpoint;
 * the saved hash and edits remain unchanged until a successful retry. */
const tic_cartridge *tm_studio_session_cart(const tm_studio_session *session);
const u32 *tm_studio_session_screen(const tm_studio_session *session);
const s16 *tm_studio_session_audio(const tm_studio_session *session);
const u32 *tm_studio_session_persistent(const tm_studio_session *session);
const CartName *tm_studio_session_name(const tm_studio_session *session);
const tm_studio_code_view *tm_studio_session_code_view(const tm_studio_session *session);
int tm_studio_session_mode(const tm_studio_session *session);
bool tm_studio_session_modified(const tm_studio_session *session);
bool tm_studio_session_selecting(const tm_studio_session *session);
bool tm_studio_session_save_error(const tm_studio_session *session);
pid_t tm_studio_session_pid(const tm_studio_session *session);
#endif
