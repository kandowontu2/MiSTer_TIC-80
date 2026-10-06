#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/backend.h"
#include "tic80_mister/memory_map.h"
#include "tic80_mister/linux_wheel.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

static void barrier(void)
{
#if defined(__arm__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}
static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static void pause_poll(void)
{
    struct timespec t = {0, 200000};
    nanosleep(&t, NULL);
}
static uint32_t read_reg(tm_backend *b, unsigned offset)
{
    uint32_t v = *(volatile uint32_t *)(b->memory + offset);
    barrier();
    return v;
}
static void write_reg(tm_backend *b, unsigned offset, uint32_t value)
{
    barrier();
    *(volatile uint32_t *)(b->memory + offset) = value;
    barrier();
}
static int core_name_state(const tm_backend *b)
{
    if (!b->core_name_path) return 1;
    FILE *f = fopen(b->core_name_path, "rb");
    if (!f) return -2;
    char name[80];
    size_t length = fread(name, 1, sizeof name, f);
    int read_error = ferror(f);
    fclose(f);
    if (read_error || !length) return -2;
    if (length == sizeof name || memchr(name, 0, length)) return -1;
    while (length && (name[length-1] == '\r' || name[length-1] == '\n')) --length;
    if (!length || memchr(name, '\r', length) || memchr(name, '\n', length)) return -1;
    name[length] = 0;
    return !strcmp(name, "TIC-80");
}
int tm_backend_core_selected(const tm_backend *b)
{
    int selected = core_name_state(b);
    if (selected != -2) return selected;
    // Main rewrites CORENAME using fopen("w") followed by fwrite/fclose.
    // During that short empty-file interval, withhold all DDR operations and
    // retry the selection instead of resetting an otherwise healthy cartridge.
    uint64_t until = now_ms() + 50;
    do {
        pause_poll();
        selected = core_name_state(b);
    } while (selected == -2 && now_ms() < until);
    return selected == -2 ? -1 : selected;
}
int tm_backend_core_generation(const tm_backend *b, tm_core_generation *generation)
{
    if (!generation) return -1;
    if (!b->core_name_path) return 0;
    struct stat metadata;
    if (tm_backend_core_selected(b) != 1 || stat(b->core_name_path, &metadata) ||
            tm_backend_core_selected(b) != 1) return -1;
    memset(generation, 0, sizeof *generation);
    generation->device = metadata.st_dev;
    generation->inode = metadata.st_ino;
    generation->modified_seconds = metadata.st_mtim.tv_sec;
    generation->modified_nanos = metadata.st_mtim.tv_nsec;
    generation->changed_seconds = metadata.st_ctim.tv_sec;
    generation->changed_nanos = metadata.st_ctim.tv_nsec;
    return 1;
}
static int identity_ok(tm_backend *b)
{
    return tm_backend_core_selected(b) == 1 && read_reg(b, TM_IDENTITY_OFFSET) == TM_MAGIC &&
        read_reg(b, TM_GEOMETRY_OFFSET) == ((TM_HEIGHT << 16) | TM_WIDTH);
}
static int reserved_memory(void)
{
    FILE *f = fopen("/proc/iomem", "r");
    if (!f) return 0;
    char line[256];
    int found_ram = 0, safe = 1;
    while (fgets(line, sizeof line, f)) {
        unsigned long long start, end;
        if (strstr(line, "System RAM") && sscanf(line, " %llx-%llx", &start, &end) == 2) {
            found_ram = 1;
            if (start < (uint64_t)TM_PHYSICAL_BASE + TM_REGION_BYTES && end >= TM_PHYSICAL_BASE) safe = 0;
        }
    }
    fclose(f);
    return found_ram && safe;
}
int tm_backend_open(tm_backend *b, const char *test_file)
{
    memset(b, 0, sizeof *b);
    b->fd = -1;
    b->physical_input = !test_file;
    b->core_name_path = test_file ? NULL : "/tmp/CORENAME";
    if (!test_file) {
#if !defined(__arm__)
        fprintf(stderr, "Physical DDR transport requires the MiSTer ARM CPU\n");
        return -1;
#endif
        if (!reserved_memory()) {
            fprintf(stderr, "DDR payload overlaps kernel RAM, or memory map is unavailable\n");
            return -1;
        }
    }
    b->fd = open(test_file ? test_file : "/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (b->fd < 0) { perror("DDR open"); return -1; }
    if (test_file) {
        struct stat st;
        if (fstat(b->fd, &st) || !S_ISREG(st.st_mode) || st.st_size < TM_REGION_BYTES) goto fail;
    }
    void *mapped = mmap(NULL, TM_REGION_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED,
                        b->fd, test_file ? 0 : (off_t)TM_PHYSICAL_BASE);
    if (mapped == MAP_FAILED) { perror("DDR mmap"); goto fail; }
    b->memory = mapped;
    uint64_t limit = now_ms() + 1000;
    while (!identity_ok(b) && now_ms() < limit) pause_poll();
    if (!identity_ok(b)) {
        fprintf(stderr, "Matching TIC-80 FPGA protocol/geometry not found\n");
        goto fail;
    }
    uint32_t heartbeat = read_reg(b, TM_HEARTBEAT_OFFSET);
    limit = now_ms() + 1000;
    while (identity_ok(b) && read_reg(b, TM_HEARTBEAT_OFFSET) == heartbeat && now_ms() < limit) pause_poll();
    if (!identity_ok(b) || read_reg(b, TM_HEARTBEAT_OFFSET) == heartbeat) {
        fprintf(stderr, "FPGA heartbeat stopped\n");
        goto fail;
    }
    b->cart_seen = read_reg(b, TM_CART_ACK_OFFSET);
    return 0;
fail:
    tm_backend_close(b);
    return -1;
}
int tm_backend_start(tm_backend *b)
{
    if (!b->memory || b->session || !identity_ok(b)) return -1;
    if (flock(b->fd, LOCK_EX | LOCK_NB)) {
        fprintf(stderr, "A runtime already owns the DDR transport\n");
        return -1;
    }
    uint32_t nonce = read_reg(b, TM_SESSION_REQUEST_OFFSET) + 1;
    uint32_t previous_ack = read_reg(b, TM_SESSION_ACK_OFFSET);
    while (!nonce || nonce == previous_ack) ++nonce;
    write_reg(b, TM_VIDEO_PUBLISH_OFFSET, 0);
    write_reg(b, TM_AUDIO_WRITE_OFFSET, 0);
    write_reg(b, TM_SESSION_REQUEST_OFFSET, nonce);
    uint64_t limit = now_ms() + 1000;
    while (identity_ok(b) && read_reg(b, TM_SESSION_ACK_OFFSET) != nonce && now_ms() < limit) pause_poll();
    if (!identity_ok(b) || read_reg(b, TM_SESSION_ACK_OFFSET) != nonce) {
        if (tm_backend_core_selected(b) == 1) fprintf(stderr, "FPGA session handshake timed out\n");
        if (identity_ok(b)) write_reg(b, TM_SESSION_REQUEST_OFFSET, 0);
        return -1;
    }
    b->session = nonce;
    tm_linux_wheel_close(b->linux_wheel);
    b->linux_wheel = NULL;
    /* The session ACK and cleared presented word are separate DDR writes. */
    limit = now_ms() + 1000;
    while (identity_ok(b) && (read_reg(b, TM_VIDEO_PRESENTED_OFFSET) != 0 || read_reg(b, TM_AUDIO_READ_OFFSET) != 0)
            && now_ms() < limit) pause_poll();
    if (!identity_ok(b) || read_reg(b, TM_VIDEO_PRESENTED_OFFSET) != 0 || read_reg(b, TM_AUDIO_READ_OFFSET) != 0) return -1;
    b->audio_write = 0;
    b->video_wait_started_ms = 0;
    b->deferred_valid = 0;
    tm_pacer_init(&b->pacer);
    tm_exchange_init(&b->exchange);
    return 0;
}
static int session_ok(tm_backend *b)
{
    return b->session && identity_ok(b) && read_reg(b, TM_SESSION_ACK_OFFSET) == b->session &&
        read_reg(b, TM_SESSION_REQUEST_OFFSET) == b->session;
}
int tm_backend_restart(tm_backend *b)
{
    if (!b->memory || !identity_ok(b)) return -1;
    b->session = 0;
    return tm_backend_start(b);
}
uint32_t tm_backend_status(tm_backend *b) { return read_reg(b, TM_STATUS_OFFSET); }
int tm_backend_inputs(tm_backend *b, tm_input_snapshot *snapshot)
{
    if (!snapshot || !session_ok(b)) return -1;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        uint32_t sequence = read_reg(b, TM_KEYBOARD_OFFSET);
        if (sequence & 1) continue;
        tm_input_snapshot next;
        for (unsigned i = 0; i < 16; ++i) next.keys[i] = read_reg(b, TM_KEYBOARD_BITS_OFFSET + i*4);
        next.mouse = read_reg(b, TM_MOUSE_OFFSET);
        next.wheel = read_reg(b, TM_MOUSE_OFFSET + 4);
        // The geometry capability comes from the current FPGA, unlike unused
        // DDR bytes which can survive a core reload. Old bitstreams yield zero.
        uint32_t capability = read_reg(b, TM_GEOMETRY_OFFSET + 4);
        next.mouse_gate = capability == TM_LINUX_INPUT_MAGIC ? (next.mouse >> 19) | 8192u : 0;
        next.horizontal_wheel = capability == TM_INPUT_EXTENSION_MAGIC || capability == TM_LINUX_INPUT_MAGIC ?
            read_reg(b, TM_HORIZONTAL_WHEEL_OFFSET) : 0;
        for (unsigned i = 0; i < 4; ++i) next.joystick[i] = read_reg(b, TM_JOY0_OFFSET + i*8);
        if (sequence == read_reg(b, TM_KEYBOARD_OFFSET)) {
            if (!session_ok(b)) return -1;
            if (b->physical_input && capability == TM_LINUX_INPUT_MAGIC) {
                if (!b->linux_wheel) b->linux_wheel = tm_linux_wheel_open(NULL);
                if (!b->linux_wheel) return -1;
                next.horizontal_wheel = tm_linux_wheel_poll(b->linux_wheel, now_ms(),
                    !(next.mouse_gate & 4096) && !(read_reg(b, TM_STATUS_OFFSET) & 1), next.mouse_gate & 4095);
                if (!session_ok(b)) return -1;
                // Device discovery/draining can overlap a new FPGA input
                // snapshot. Re-read its gate before returning Linux events.
                if (sequence != read_reg(b, TM_KEYBOARD_OFFSET)) continue;
            }
            b->inputs = next;
            *snapshot = next;
            return 1;
        }
    }
    if (!session_ok(b)) return -1;
    *snapshot = b->inputs;
    return 0;
}
int tm_backend_cart_pending(tm_backend *b)
{
    if (!session_ok(b)) return -1;
    uint32_t ticket = read_reg(b, TM_CART_META_OFFSET);
    return ticket != b->cart_seen && (ticket & 3) >= 2;
}
int tm_backend_cart_source(tm_backend *b, uint8_t **bytes, size_t *size, char source[256])
{
    if (!bytes || !size) return -1;
    *bytes = NULL;
    *size = 0;
    if(source) source[0]=0;
    if (!session_ok(b)) return -1;
    uint32_t ticket = read_reg(b, TM_CART_META_OFFSET);
    if (ticket == b->cart_seen || (ticket & 3) < 2) return 0;
    uint32_t length = read_reg(b, TM_CART_META_OFFSET + 4);
    if (ticket != read_reg(b, TM_CART_META_OFFSET)) return 0;
    int result = 2;
    uint8_t *copy = NULL;
    char private_source[256] = {0};
    if ((ticket & 3) == 2 && length >= 4 && length <= TM_CART_CAPACITY) {
        copy = malloc(length);
        if (!copy) return -1;
        memcpy(copy, (const void *)(b->memory + TM_CART_DATA_OFFSET), length);
        if(source && read_reg(b,TM_IDENTITY_OFFSET+4)==TM_CART_SOURCE_MAGIC &&
                read_reg(b,TM_CART_SOURCE_OFFSET)==ticket) {
            uint32_t path_length=read_reg(b,TM_CART_SOURCE_OFFSET+4);
            if(path_length>=2 && path_length<=TM_CART_SOURCE_CAPACITY) {
                memcpy(private_source,(const void*)(b->memory+TM_CART_SOURCE_OFFSET+8),path_length);
                if(private_source[0]!='/' || private_source[path_length-1] || memchr(private_source,0,path_length-1)) private_source[0]=0;
            }
        }
        result = 1;
    }
    barrier();
    // Rejected transfers also require renewed ownership before their ACK.
    // Keep all outputs private until both the session and ticket still match.
    if (!session_ok(b) || ticket != read_reg(b, TM_CART_META_OFFSET)) {
        free(copy);
        return -1;
    }
    /* Only release the staging memory once the whole private copy is safe.
     * Bad transfers also need an ACK so the OSD can send a replacement. */
    write_reg(b, TM_CART_ACK_OFFSET, ticket);
    b->cart_seen = ticket;
    if (result == 1) {
        *bytes = copy;
        *size = length;
        if(source) strcpy(source,private_source);
    }
    return result;
}
int tm_backend_cart(tm_backend *b,uint8_t **bytes,size_t *size)
{ return tm_backend_cart_source(b,bytes,size,NULL); }
enum { VIDEO_BLOCK, VIDEO_STREAM, VIDEO_PUMP };
static int present(tm_backend *b, const uint8_t *frame, size_t size, int mode)
{
    if (!frame || size != TM_FRAME_BYTES || !session_ok(b)) return -1;
    unsigned buffer;
    int acquired;
    while ((acquired = tm_exchange_acquire(&b->exchange,
                read_reg(b, TM_VIDEO_PRESENTED_OFFSET), &buffer)) == 0) {
        if (!session_ok(b)) return -1;
        uint64_t now = now_ms();
        if (!b->video_wait_started_ms) b->video_wait_started_ms = now;
        if (now - b->video_wait_started_ms >= 1000) {
            fprintf(stderr, "FPGA frame acknowledgment timed out\n");
            return -1;
        }
        if (mode != VIDEO_BLOCK) {
            uint32_t queued = b->audio_write - read_reg(b, TM_AUDIO_READ_OFFSET);
            if (queued > TM_AUDIO_CAPACITY) return -1;
            if (mode == VIDEO_STREAM) {
                if (!b->deferred_frame) b->deferred_frame = malloc(TM_FRAME_BYTES);
                if (!b->deferred_frame) return -1;
                memcpy(b->deferred_frame, frame, TM_FRAME_BYTES);
                b->deferred_valid = 1;
            }
            // Only the latest private picture may be replaced. Both DDR
            // buffers and their pending publication stay immutable.
            return 0;
        }
        pause_poll();
    }
    if (acquired < 0) return -1;
    // Scanout can release the buffer while Main is changing cores. The last
    // wait's selection check predates acquisition; renew it before DDR writes.
    if (!session_ok(b)) return -1;
    b->video_wait_started_ms = 0;
    unsigned offset = buffer ? TM_BUFFER1_OFFSET : TM_BUFFER0_OFFSET;
    memcpy((void *)(b->memory + offset), frame, size);
    barrier();
    if (!session_ok(b)) return -1;
    uint32_t publication;
    if (tm_exchange_publish(&b->exchange, &publication)) return -1;
    write_reg(b, TM_VIDEO_PUBLISH_OFFSET, publication);
    b->deferred_valid = 0;
    return 0;
}
int tm_backend_present(tm_backend *b, const uint8_t *frame, size_t size)
{
    return present(b, frame, size, VIDEO_BLOCK);
}
int tm_backend_present_realtime(tm_backend *b, const uint8_t *frame, size_t size)
{
    return present(b, frame, size, VIDEO_STREAM);
}
int tm_backend_drain(tm_backend *b)
{
    uint64_t limit = now_ms() + 1000;
    while (b->deferred_valid || (b->exchange.pending && read_reg(b, TM_VIDEO_PRESENTED_OFFSET) != b->exchange.publication)
           || read_reg(b, TM_AUDIO_READ_OFFSET) != b->audio_write) {
        if (!session_ok(b) || now_ms() >= limit) return -1;
        if (b->deferred_valid && present(b, b->deferred_frame, TM_FRAME_BYTES, VIDEO_PUMP)) return -1;
        pause_poll();
    }
    return 0;
}
int tm_backend_audio(tm_backend *b, const int16_t *stereo, unsigned frames)
{
    if (!stereo || !frames || (frames & 1) || frames > TM_AUDIO_CAPACITY || !session_ok(b)) return -1;
    uint32_t space;
    uint64_t limit = now_ms() + 1000;
    for (;;) {
        if (tm_audio_available(b->audio_write, read_reg(b, TM_AUDIO_READ_OFFSET), &space)) return -1;
        if (space >= frames) break;
        if (!session_ok(b)) return -1;
        if (now_ms() >= limit) {
            fprintf(stderr, "FPGA audio ring stalled\n");
            return -1;
        }
        pause_poll();
    }
    unsigned index = b->audio_write & (TM_AUDIO_CAPACITY - 1);
    if (!session_ok(b)) return -1;
    unsigned first = TM_AUDIO_CAPACITY - index;
    if (first > frames) first = frames;
    memcpy((void *)(b->memory + TM_AUDIO_RING_OFFSET + index * 4), stereo, first * 4);
    if (first < frames) memcpy((void *)(b->memory + TM_AUDIO_RING_OFFSET), stereo + first * 2, (frames - first) * 4);
    if (!session_ok(b)) return -1;
    b->audio_write += frames;
    write_reg(b, TM_AUDIO_WRITE_OFFSET, b->audio_write);
    return 0;
}
int tm_backend_pace(tm_backend *b, const volatile sig_atomic_t *cancel)
{
    if (!session_ok(b)) return -1;
    tm_pacer_frame(&b->pacer, now_ns());
    uint64_t limit = now_ns() + 1000000000ULL;
    for (;;) {
        if (cancel && *cancel) return 0;
        if (!session_ok(b)) return -1;
        if (b->deferred_valid && present(b, b->deferred_frame, TM_FRAME_BYTES, VIDEO_PUMP)) return -1;
        int64_t delay = tm_pacer_delay(&b->pacer, b->audio_write,
            read_reg(b, TM_AUDIO_READ_OFFSET), now_ns(), TM_AUDIO_CAPACITY);
        if (delay < 0) return -1;
        if (!delay) return 0;
        if (now_ns() >= limit) {
            fprintf(stderr, "FPGA audio clock stalled\n");
            return -1;
        }
        // Keep departure detection bounded while sleeping until playback credit.
        if (delay > 1000000) delay = 1000000;
        struct timespec wait = {0, (long)delay};
        nanosleep(&wait, NULL);
    }
}
int tm_backend_work_budget(tm_backend *b,unsigned reserve,unsigned maximum_ms)
{
    if(!session_ok(b)) return -1;
    return tm_pacer_work_budget(&b->pacer,b->audio_write,
        read_reg(b,TM_AUDIO_READ_OFFSET),TM_AUDIO_CAPACITY,reserve,maximum_ms);
}
uint32_t tm_backend_gamepads(tm_backend *b)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t raw = read_reg(b, TM_JOY0_OFFSET + i * 8);
        /* MiSTer R,L,D,U -> TIC U,D,L,R; A,B,X,Y retain their bit positions. */
        uint32_t pad = ((raw >> 3) & 1) | ((raw >> 1) & 2) |
                       ((raw << 1) & 4) | ((raw << 3) & 8) | (raw & 0xF0);
        result |= pad << (8 * i);
    }
    return result;
}
void tm_backend_close(tm_backend *b)
{
    tm_linux_wheel_close(b->linux_wheel);
    b->linux_wheel = NULL;
    free(b->deferred_frame);
    b->deferred_frame = NULL;
    b->deferred_valid = 0;
    if (b->memory) {
        if (b->session && identity_ok(b) && read_reg(b, TM_SESSION_ACK_OFFSET) == b->session &&
                read_reg(b, TM_SESSION_REQUEST_OFFSET) == b->session)
            write_reg(b, TM_SESSION_REQUEST_OFFSET, 0);
        munmap((void *)b->memory, TM_REGION_BYTES);
    }
    if (b->fd >= 0) close(b->fd);
    b->memory = NULL;
    b->fd = -1;
    b->session = 0;
    b->core_name_path = NULL;
}
