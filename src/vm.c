#define _GNU_SOURCE
#include "tic80_mister/vm.h"
#include "tic80_mister/cart.h"
#include "tic80_mister/cart_file.h"
#include "tic80_mister/pmem.h"
#include "tic.h"
#include "api.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CART_CAPACITY (4u * 1024u * 1024u)
#define PIXELS (TIC80_FULLWIDTH * TIC80_FULLHEIGHT)
#define SAMPLES 1600
#define INIT_MS 5000
#define TICK_MS 1000
#define MAGIC 0x544d564d
typedef struct {
    uint32_t magic, length;
    tic80_input input;
    uint64_t offset;
    int result, sample_count;
    tm_fft_config capture;
    int capture_status;
    char key[33];
    uint32_t persistent[256], screen[PIXELS];
    int16_t samples[SAMPLES];
    uint8_t cartridge[CART_CAPACITY];
} shared_vm;
struct tm_vm { pid_t pid; int socket; shared_vm *shared; tic80 *product; int first, capture_status; tm_fft_config capture; char key[33]; };
extern char **environ;
static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void reap(tm_vm *vm)
{
    if (vm->pid > 0) {
        kill(vm->pid, SIGKILL);
        while (waitpid(vm->pid, NULL, 0) < 0 && errno == EINTR) {}
        vm->pid = 0;
    }
}
static int response(tm_vm *vm, unsigned timeout)
{
    uint64_t deadline = now_ms() + timeout;
    struct pollfd fd = {vm->socket, POLLIN, 0};
    for (;;) {
        uint64_t now = now_ms();
        if (now >= deadline) { reap(vm); return TM_VM_TIMEOUT; }
        int ready = poll(&fd, 1, (int)(deadline - now));
        /* Signal interruption lets the live parent's SIGTERM handler stop
         * promptly even when the interpreter cannot service signals. */
        if (ready < 0) { reap(vm); return TM_VM_DIED; }
        if (!ready) continue;
        char message;
        if (recv(vm->socket, &message, 1, MSG_DONTWAIT) != 1 || message != 'R') {
            reap(vm); return TM_VM_DIED;
        }
        atomic_thread_fence(memory_order_acquire);
        int result = vm->shared->result;
        if (result < TM_VM_OK || result > TM_VM_DIED) { reap(vm); return TM_VM_DIED; }
        return result;
    }
}
static int request(tm_vm *vm, char message, unsigned timeout)
{
    if (!vm || vm->pid <= 0) return TM_VM_DIED;
    atomic_thread_fence(memory_order_release);
    if (send(vm->socket, &message, 1, MSG_NOSIGNAL) != 1) { reap(vm); return TM_VM_DIED; }
    return response(vm, timeout);
}
void tm_vm_close(tm_vm *vm)
{
    if (!vm) return;
    if (vm->pid > 0) request(vm, 'Q', 500);
    reap(vm);
    if (vm->socket >= 0) close(vm->socket);
    if (vm->shared != MAP_FAILED) munmap(vm->shared, sizeof *vm->shared);
    if (vm->product) tic80_delete(vm->product);
    free(vm);
}
int tm_vm_open_configured(tm_vm **out, const uint8_t *bytes, size_t size,const tm_fft_config *capture)
{
    if(!out) return TM_VM_ERROR;
    *out = NULL;
    const tm_fft_config disabled={0};
    if(!capture) capture=&disabled;
    if(!tm_fft_config_valid(capture)) return TM_VM_ERROR;
    if (tm_cart_file_validate(bytes, size)) return TM_VM_ERROR;
    tm_vm *vm = calloc(1, sizeof *vm);
    if (!vm) return TM_VM_ERROR;
    vm->socket = -1; vm->shared = MAP_FAILED; vm->first = 1;
    vm->capture=*capture;
    int memory = -1, pair[2] = {-1, -1}, memory_source = -1, socket_source = -1;
    int result = TM_VM_ERROR;
    memory = memfd_create("tic80-vm", MFD_CLOEXEC);
    if (memory < 0 || ftruncate(memory, sizeof(shared_vm))) goto done;
    vm->shared = mmap(NULL, sizeof(shared_vm), PROT_READ | PROT_WRITE, MAP_SHARED, memory, 0);
    if (vm->shared == MAP_FAILED) goto done;
    vm->shared->magic = MAGIC; vm->shared->length = (uint32_t)size;
    vm->shared->capture=*capture;
    memcpy(vm->shared->cartridge, bytes, size);
    vm->product = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    if (!vm->product || socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) goto done;
    /* Source descriptors stay above the fixed child descriptors, avoiding
     * dup2 collisions regardless of the parent's existing descriptor layout. */
    memory_source = fcntl(memory, F_DUPFD_CLOEXEC, 10);
    socket_source = fcntl(pair[1], F_DUPFD_CLOEXEC, 10);
    if (memory_source < 0 || socket_source < 0) goto done;
    posix_spawn_file_actions_t actions;
    int action_error = posix_spawn_file_actions_init(&actions);
    if (action_error) goto done;
    action_error = posix_spawn_file_actions_adddup2(&actions, memory_source, 3);
    if (!action_error) action_error = posix_spawn_file_actions_adddup2(&actions, socket_source, 4);
    char parent[32]; snprintf(parent, sizeof parent, "%ld", (long)getpid());
    char *arguments[] = {"tic80-vm", "--vm-worker", parent, NULL};
    atomic_thread_fence(memory_order_release);
    if (!action_error) action_error = posix_spawn(&vm->pid, "/proc/self/exe", &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (action_error) goto done;
    vm->socket = pair[0]; pair[0] = -1;
    close(pair[1]); pair[1] = -1;
    close(socket_source); socket_source = -1;
    result = response(vm, INIT_MS);
    if (result == TM_VM_OK) {
        if(!tm_fft_status_valid(&vm->capture,vm->shared->capture_status) || vm->shared->capture_status==TM_FFT_PAUSED) result=TM_VM_DIED;
        vm->capture_status=vm->shared->capture_status;
        memcpy(vm->key, vm->shared->key, sizeof vm->key);
        for (unsigned i = 0; i < 32; ++i)
            if (!strchr("0123456789abcdef", vm->key[i]) || !vm->key[i]) result = TM_VM_DIED;
        if (vm->key[32]) result = TM_VM_DIED;
        if (result == TM_VM_OK) { *out = vm; vm = NULL; }
    }
done:
    if (memory >= 0) close(memory);
    if (memory_source >= 0) close(memory_source);
    if (socket_source >= 0) close(socket_source);
    if (pair[0] >= 0) close(pair[0]);
    if (pair[1] >= 0) close(pair[1]);
    tm_vm_close(vm);
    return result;
}
int tm_vm_open(tm_vm **out,const uint8_t *bytes,size_t size)
{ return tm_vm_open_configured(out,bytes,size,NULL); }
int tm_vm_fft_status(const tm_vm *vm) { return vm?vm->capture_status:TM_FFT_UNAVAILABLE; }
static int capture_request(tm_vm *vm,char command)
{
    if(!vm || vm->pid<=0) return TM_VM_DIED;
    if(!vm->capture.enabled) return TM_VM_OK;
    int result=request(vm,command,INIT_MS);
    if(result!=TM_VM_OK) return result;
    int status=vm->shared->capture_status;
    if(!tm_fft_status_valid(&vm->capture,status) ||
        (command=='P' ? status!=TM_FFT_PAUSED : status==TM_FFT_PAUSED))
    { reap(vm); return TM_VM_DIED; }
    vm->capture_status=status; return TM_VM_OK;
}
int tm_vm_fft_pause(tm_vm *vm) { return capture_request(vm,'P'); }
int tm_vm_fft_resume(tm_vm *vm) { return capture_request(vm,'F'); }
tic80 *tm_vm_product(tm_vm *vm) { return vm ? vm->product : NULL; }
const char *tm_vm_key(tm_vm *vm) { return vm ? vm->key : NULL; }
int tm_vm_tick(tm_vm *vm, tic80_input input, uint64_t offset)
{
    if (!vm || vm->pid <= 0) return TM_VM_DIED;
    shared_vm *shared = vm->shared;
    shared->input = input; shared->offset = offset;
    if (vm->first) memcpy(shared->persistent, ((tic_mem *)vm->product)->ram->persistent.data, sizeof shared->persistent);
    /* The first tick also compiles the language and runs BOOT. Fennel's
     * cold compiler can exceed one second on ARM; later frames get 1s. */
    int result = request(vm, 'T', vm->first ? INIT_MS : TICK_MS);
    if (result != TM_VM_OK && result != TM_VM_ERROR && result != TM_VM_EXIT) return result;
    if (shared->sample_count != SAMPLES) { reap(vm); return TM_VM_DIED; }
    memcpy(vm->product->screen, shared->screen, sizeof shared->screen);
    memcpy(vm->product->samples.buffer, shared->samples, sizeof shared->samples);
    vm->product->samples.count = SAMPLES;
    memcpy(((tic_mem *)vm->product)->ram->persistent.data, shared->persistent, sizeof shared->persistent);
    vm->first = 0;
    return result;
}

static int worker_error, worker_exit;
static uint64_t worker_offset;
static void on_error(const char *message) { fprintf(stderr, "TIC-80: %s\n", message); worker_error = 1; }
static void on_trace(const char *message, u8 color) { (void)color; fprintf(stderr, "%s\n", message); }
static void vm_on_exit(void) { worker_exit = 1; }
static u64 counter(void *data)
{
    (void)data; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ULL + t.tv_nsec - worker_offset;
}
static u64 frequency(void *data) { (void)data; return 1000000000ULL; }
static int reply(shared_vm *shared, int result)
{
    shared->result = result;
    atomic_thread_fence(memory_order_release);
    return send(4, "R", 1, MSG_NOSIGNAL) == 1 ? 0 : -1;
}
int tm_vm_worker(int argc, char **argv)
{
    if (argc != 3) return 2;
    char *end; errno = 0; long parent = strtol(argv[2], &end, 10);
    if (errno || !*argv[2] || *end || parent <= 1) return 2;
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) return 1;
    prctl(PR_SET_NAME, "tic80-vm");
    // ASAN reserves a large virtual shadow range. Enforce the production
    // interpreter budget only in ordinary builds, as in the Studio worker.
#ifndef __SANITIZE_ADDRESS__
    struct rlimit limit = {128u * 1024u * 1024u, 128u * 1024u * 1024u};
    if (setrlimit(RLIMIT_AS, &limit)) return 1;
#endif
    struct rlimit cores = {0, 0};
    if (setrlimit(RLIMIT_CORE, &cores)) return 1;
    struct stat info;
    if (fstat(3, &info) || info.st_size != sizeof(shared_vm)) return 2;
    shared_vm *shared = mmap(NULL, sizeof *shared, PROT_READ | PROT_WRITE, MAP_SHARED, 3, 0);
    if (shared == MAP_FAILED) return 1;
    close(3);
    atomic_thread_fence(memory_order_acquire);
    if (shared->magic != MAGIC || shared->length > CART_CAPACITY || !tm_fft_config_valid(&shared->capture)) return 2;
    uint8_t *native=NULL; size_t native_size=0;
    if (tm_cart_file_decode(shared->cartridge,shared->length,&native,&native_size)) {
        reply(shared,TM_VM_ERROR); _exit(1);
    }
    tic80 *tic = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    if (!tic) { free(native); return 1; }
    tic->callback.error = on_error; tic->callback.trace = on_trace; tic->callback.exit = vm_on_exit;
    tic80_load(tic,native,(s32)native_size); free(native);
    shared->capture_status=tm_fft_worker_start(&shared->capture);
    tm_pmem_key(tic, shared->key);
    if (reply(shared, TM_VM_OK)) _exit(1);
    int first = 1;
    for (;;) {
        char message;
        if (recv(4, &message, 1, 0) != 1) break;
        atomic_thread_fence(memory_order_acquire);
        if (message == 'Q') break;
        if(message=='P' || message=='F') {
            if(message=='P') { tm_fft_worker_close(); shared->capture_status=shared->capture.enabled?TM_FFT_PAUSED:TM_FFT_DISABLED; }
            else shared->capture_status=tm_fft_worker_start(&shared->capture);
            if(reply(shared,TM_VM_OK)) _exit(1);
            continue;
        }
        if (message != 'T') _exit(2);
        if (first) memcpy(((tic_mem *)tic)->ram->persistent.data, shared->persistent, sizeof shared->persistent);
        worker_offset = shared->offset;
        worker_error = worker_exit = 0;
        tic80_tick(tic, shared->input, counter, frequency);
        tic80_sound(tic);
        if (tic->samples.count != SAMPLES) _exit(2);
        memcpy(shared->screen, tic->screen, sizeof shared->screen);
        memcpy(shared->samples, tic->samples.buffer, sizeof shared->samples);
        shared->sample_count = SAMPLES;
        memcpy(shared->persistent, ((tic_mem *)tic)->ram->persistent.data, sizeof shared->persistent);
        first = 0;
        if (reply(shared, worker_error ? TM_VM_ERROR : worker_exit ? TM_VM_EXIT : TM_VM_OK)) _exit(1);
    }
    tm_fft_worker_close(); tic80_delete(tic);
    reply(shared, TM_VM_OK);
    _exit(0);
}
