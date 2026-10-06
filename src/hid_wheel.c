#define _GNU_SOURCE
#include "tic80_mister/hid_wheel.h"
#include "tic80_mister/hid_pan.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { DEVICES = 64, REPORTS_PER_DEVICE = 512, MESSAGES_PER_POLL = 64,
       RETIRED_WORKERS = 32, WORKER_SOCKET = 3 };
enum { COMMAND_MAGIC = 0x43485754, RESPONSE_MAGIC = 0x52485754 };
typedef struct { uint32_t magic, value; uint64_t generation; } message;
_Static_assert(sizeof(message) == 16, "HID worker wire size");
typedef struct {
    int fd, seen, usable, retryable;
    unsigned failures;
    int probe_socket;
    pid_t probe_pid;
    uint64_t probe_deadline, next_probe;
    char name[32];
    dev_t device, special; ino_t inode; struct timespec changed;
    tm_hid_pan pan;
} device;
typedef struct { int status; tm_hid_pan pan; } probe_result;
typedef struct {
    device devices[DEVICES];
    const char *directory;
    uint64_t next_scan;
} reader;
struct tm_hid_wheel {
    char *directory;
    pid_t pid;
    int socket, initialized, active, slot;
    unsigned epoch;
    uint32_t total, child_total;
    uint64_t generation, sent_generation, last_activity, next_spawn;
};
/* Deferred, specific-child reaping keeps close/restart from waiting on a USB
 * ioctl. These functions are called by the single frontend control thread. */
static pid_t retired[RETIRED_WORKERS];
static int occupied[RETIRED_WORKERS];
extern char **environ;
static uint64_t milliseconds(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000 + (uint64_t)t.tv_nsec/1000000;
}
static void reap(void)
{
    for (unsigned n = 0; n < RETIRED_WORKERS; ++n) {
        if (!retired[n]) continue;
        pid_t result = waitpid(retired[n],NULL,WNOHANG);
        if (result > 0 || (result < 0 && errno == ECHILD)) {
            retired[n] = 0; occupied[n] = 0;
        }
    }
}
static int retirement_slot(void)
{
    reap();
    for (unsigned n = 0; n < RETIRED_WORKERS; ++n) if (!occupied[n]) return (int)n;
    return -1;
}
static void stop_worker(tm_hid_wheel *s, uint64_t now)
{
    if (s->socket >= 0) close(s->socket);
    s->socket = -1;
    if (s->pid > 0) {
        pid_t result = waitpid(s->pid,NULL,WNOHANG);
        if (!result || (result < 0 && errno == EINTR)) {
            kill(s->pid,SIGKILL);
            retired[s->slot] = s->pid;
        } else occupied[s->slot] = 0;
    }
    s->pid = 0; s->slot = -1; s->sent_generation = 0; s->child_total = 0;
    s->next_spawn = now + 1000;
}
static int start_worker(tm_hid_wheel *s, uint64_t now)
{
    int slot = retirement_slot();
    if (slot < 0) return -1;
    int sockets[2];
    if (socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0,sockets)) return -1;
    /* A high temporary fd prevents an inherited backend fd 3, or either socket
     * endpoint, from colliding with the worker's fixed control descriptor. */
    int source = fcntl(sockets[1],F_DUPFD_CLOEXEC,10);
    close(sockets[1]);
    if (source < 0) { close(sockets[0]); return -1; }
    char parent[32]; snprintf(parent,sizeof parent,"%ld",(long)getpid());
    char *arguments[] = {"tic80-hid-wheel","--hid-wheel-worker",s->directory,parent,NULL};
    posix_spawn_file_actions_t actions;
    int status = posix_spawn_file_actions_init(&actions);
    if (!status) {
        status = posix_spawn_file_actions_adddup2(&actions,source,WORKER_SOCKET);
        if (!status) status = posix_spawn(&s->pid,"/proc/self/exe",&actions,NULL,arguments,environ);
        posix_spawn_file_actions_destroy(&actions);
    }
    close(source);
    if (status) { close(sockets[0]); s->pid = 0; return -1; }
    s->slot = slot; occupied[slot] = 1;
    s->socket = sockets[0]; s->sent_generation = 0; s->child_total = 0;
    ++s->generation; if (!s->generation) ++s->generation;
    s->last_activity = now;
    return 0;
}
tm_hid_wheel *tm_hid_wheel_open(const char *directory)
{
    tm_hid_wheel *s = calloc(1,sizeof *s);
    if (!s) return NULL;
    s->socket = -1; s->slot = -1;
    s->directory = strdup(directory ? directory : "/dev");
    if (!s->directory) { free(s); return NULL; }
    return s;
}
uint32_t tm_hid_wheel_poll(tm_hid_wheel *s, uint64_t now, int active, unsigned epoch)
{
    if (!s) return 0;
    reap(); active = !!active;
    if (!s->initialized || s->active != active || s->epoch != epoch) {
        s->initialized = 1; s->active = active; s->epoch = epoch;
        ++s->generation; if (!s->generation) ++s->generation;
        s->child_total = 0;
    }
    if (s->pid > 0) {
        pid_t result = waitpid(s->pid,NULL,WNOHANG);
        if (result > 0 || (result < 0 && errno == ECHILD)) {
            stop_worker(s,now);
        }
    }
    if (!s->pid && now >= s->next_spawn) {
        if (start_worker(s,now)) s->next_spawn = now + 1000;
    }
    if (!s->pid) return s->total;
    if (s->sent_generation != s->generation) {
        message command = {COMMAND_MAGIC,(uint32_t)active,s->generation};
        ssize_t bytes = send(s->socket,&command,sizeof command,MSG_DONTWAIT|MSG_NOSIGNAL);
        if (bytes == sizeof command) s->sent_generation = s->generation;
        else if (bytes >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            stop_worker(s,now); return s->total;
        }
    }
    for (unsigned n = 0; n < MESSAGES_PER_POLL; ++n) {
        message response;
        ssize_t bytes = recv(s->socket,&response,sizeof response,MSG_DONTWAIT|MSG_TRUNC);
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
        if (bytes != sizeof response || response.magic != RESPONSE_MAGIC || !response.generation) {
            stop_worker(s,now); return s->total;
        }
        if (response.generation != s->generation) continue;
        s->last_activity = now;
        if (active) s->total += response.value - s->child_total;
        s->child_total = response.value;
    }
    if (now - s->last_activity > 3000) stop_worker(s,now);
    return s->total;
}
void tm_hid_wheel_close(tm_hid_wheel *s)
{
    if (!s) { reap(); return; }
    stop_worker(s,0); free(s->directory); free(s); reap();
}
static void stop_probe(device *d)
{
    if (d->probe_socket >= 0) close(d->probe_socket);
    d->probe_socket = -1;
    if (d->probe_pid > 0) {
        pid_t result = waitpid(d->probe_pid,NULL,WNOHANG);
        if (!result || (result < 0 && errno == EINTR)) kill(d->probe_pid,SIGKILL);
        else d->probe_pid = 0;
    }
}
static void drop(device *d)
{
    stop_probe(d);
    if (d->fd >= 0) close(d->fd);
    /* A killed child may still be inside a kernel request. Keep its slot
     * reserved until it is reaped rather than forgetting a live process. */
    pid_t pending = d->probe_pid;
    memset(d,0,sizeof *d); d->fd = -1; d->probe_socket = -1;
    d->probe_pid = pending;
}
static int same(const device *d, const struct stat *s)
{
    return d->device == s->st_dev && d->special == s->st_rdev && d->inode == s->st_ino &&
        d->changed.tv_sec == s->st_ctim.tv_sec && d->changed.tv_nsec == s->st_ctim.tv_nsec;
}
static int name_valid(const char *s)
{
    if (strncmp(s,"hidraw",6) || !s[6]) return 0;
    for (s += 6; *s; ++s) if (*s < '0' || *s > '9') return 0;
    return 1;
}
static int configure(device *d)
{
    int size = 0;
    struct hidraw_report_descriptor descriptor = {0};
    if (ioctl(d->fd,HIDIOCGRDESCSIZE,&size) < 0) return -2;
    if (size <= 0 || size >= HID_MAX_DESCRIPTOR_SIZE) return -1;
    descriptor.size = (uint32_t)size;
    if (ioctl(d->fd,HIDIOCGRDESC,&descriptor) < 0 || descriptor.size != (uint32_t)size) return -2;
    int status = tm_hid_pan_parse(&d->pan,descriptor.value,descriptor.size);
    if (status <= 0) return status;
    uint8_t requested[256] = {0};
    for (unsigned n = 0; n < d->pan.multipliers; ++n) {
        unsigned id = d->pan.multiplier[n].report;
        if (!tm_hid_pan_feature_required(&d->pan,id)) continue;
        if (requested[id]) continue;
        requested[id] = 1;
        unsigned length = (d->pan.feature_bits[id]+7)/8 + 1;
        uint8_t feature[TM_HID_REPORT_BYTES+1] = {0};
        if (length > sizeof feature) return -1;
        feature[0] = (uint8_t)id;
        int bytes = ioctl(d->fd,HIDIOCGFEATURE(length),feature);
        /* USB preserves a zero report-ID prefix; Bluetooth HIDP and UHID
         * can return an unnumbered payload without it. Match the exact
         * descriptor length instead of guessing from the first data byte. */
        unsigned prefix;
        if (bytes == (int)length && feature[0] == id) prefix = 1;
        else if (!id && bytes == (int)length-1) prefix = 0;
        else return -2;
        if (tm_hid_pan_feature(&d->pan,id,feature+prefix,length-1) < 0) return -2;
    }
    return tm_hid_pan_ready(&d->pan) ? 1 : -2;
}
static void close_except(int first, int second)
{
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) _exit(1);
    int own = dirfd(dir); struct dirent *entry;
    while ((entry = readdir(dir))) {
        char *end; long fd = strtol(entry->d_name,&end,10);
        if (*entry->d_name && !*end && fd >= WORKER_SOCKET &&
            fd != first && fd != second && fd != own) close((int)fd);
    }
    closedir(dir);
}
static void retry_probe(device *d, uint64_t now)
{
    /* Back off transient transport/resource failures without requiring a
     * reconnect. The slot remains reserved while a killed child is unreaped. */
    unsigned delay = d->failures < 5 ? 1000u << d->failures : 30000u;
    if (d->failures < 5) ++d->failures;
    d->retryable = 1; d->next_probe = now + delay;
}
static void start_probe(device *d, uint64_t now)
{
    int sockets[2];
    if (socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0,sockets)) {
        retry_probe(d,now); return;
    }
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG,SIGKILL) || parent != getppid()) _exit(1);
        /* Descriptor/feature ioctls are isolated per device. A stalled USB
         * request cannot hold the reader heartbeat or another device's pan. */
        close_except(d->fd,sockets[1]);
        probe_result result = {0};
        result.status = configure(d); result.pan = d->pan;
        ssize_t sent = send(sockets[1],&result,sizeof result,MSG_NOSIGNAL);
        _exit(sent == sizeof result ? 0 : 1);
    }
    close(sockets[1]);
    if (child < 0) { close(sockets[0]); retry_probe(d,now); return; }
    d->retryable = 0;
    d->probe_pid = child; d->probe_socket = sockets[0];
    d->probe_deadline = now + 1500;
}
static uint32_t read_device(device *, int);
static void poll_probe(device *d, uint64_t now)
{
    if (!d->probe_pid) return;
    if (d->probe_socket >= 0) {
        probe_result result;
        ssize_t size = recv(d->probe_socket,&result,sizeof result,MSG_DONTWAIT|MSG_TRUNC);
        if (size == sizeof result) {
            if (result.status == -2) retry_probe(d,now);
            else d->retryable = 0;
            if (result.status == 1) {
                d->failures = 0;
                d->pan = result.pan; d->usable = 1;
                /* Reports arriving during configuration have no current gate
                 * baseline. Drain them before accepting gameplay motion. */
                read_device(d,0);
            }
            stop_probe(d);
        } else if (size >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ||
                   now >= d->probe_deadline) {
            retry_probe(d,now); stop_probe(d);
        }
    } else {
        pid_t result = waitpid(d->probe_pid,NULL,WNOHANG);
        if (result > 0 || (result < 0 && errno == ECHILD)) d->probe_pid = 0;
    }
}
static void scan(reader *s)
{
    DIR *dir = opendir(s->directory);
    if (!dir) {
        for (unsigned i = 0; i < DEVICES; ++i) drop(&s->devices[i]);
        return;
    }
    for (unsigned i = 0; i < DEVICES; ++i) s->devices[i].seen = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        struct stat metadata;
        if (!name_valid(entry->d_name) || strlen(entry->d_name) >= sizeof s->devices[0].name ||
            fstatat(dirfd(dir),entry->d_name,&metadata,AT_SYMLINK_NOFOLLOW) || !S_ISCHR(metadata.st_mode)) continue;
        device *free_slot = NULL, *found = NULL;
        for (unsigned i = 0; i < DEVICES; ++i) {
            device *d = &s->devices[i];
            if (d->fd >= 0 && !strcmp(d->name,entry->d_name)) {
                if (same(d,&metadata)) found = d; else drop(d);
            }
            if (d->fd < 0 && !d->probe_pid && !free_slot) free_slot = d;
        }
        if (found) { found->seen = 1; continue; }
        if (!free_slot) continue;
        int fd = openat(dirfd(dir),entry->d_name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
        if (fd < 0) continue;
        device *d = free_slot; struct stat opened;
        d->device = metadata.st_dev; d->special = metadata.st_rdev;
        d->inode = metadata.st_ino; d->changed = metadata.st_ctim;
        if (fstat(fd,&opened) || !same(d,&opened)) { close(fd); continue; }
        d->fd = fd; d->seen = 1; strcpy(d->name,entry->d_name);
        /* Unsupported descriptors remain cached; temporary query failures
         * retry with backoff without disturbing other devices. */
        start_probe(d,milliseconds());
    }
    closedir(dir);
    for (unsigned i = 0; i < DEVICES; ++i) if (!s->devices[i].seen) drop(&s->devices[i]);
}
static uint32_t read_device(device *d, int active)
{
    if (d->fd < 0 || !d->usable) return 0;
    if (!active) tm_hid_pan_clear(&d->pan);
    uint32_t total = 0;
    for (unsigned n = 0; n < REPORTS_PER_DEVICE; ++n) {
        uint8_t bytes[TM_HID_REPORT_BYTES+1];
        ssize_t count = read(d->fd,bytes,sizeof bytes);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return total;
        if (count < 0 && errno == EINTR) return total;
        if (count <= 0) { drop(d); return total; }
        if (active) {
            int64_t detents;
            if (tm_hid_pan_input(&d->pan,bytes,(size_t)count,&detents) > 0) total += (uint32_t)detents;
        }
    }
    /* Fresh kernel queues prevent a blocked/gated backlog from leaking into
     * gameplay. An active large burst is retained in the current stream. */
    if (!active) drop(d);
    return total;
}
static void close_inherited(void)
{
    close_except(WORKER_SOCKET,-1);
}
int tm_hid_wheel_worker(int argc, char **argv)
{
    if (argc != 4 || !argv[2] || !*argv[2]) return 2;
    char *end; errno = 0; long parent = strtol(argv[3],&end,10);
    if (errno || !*argv[3] || *end || parent <= 1 || parent != getppid()) return 2;
    if (prctl(PR_SET_PDEATHSIG,SIGKILL) || parent != getppid()) return 1;
    int type; socklen_t length = sizeof type;
    if (getsockopt(WORKER_SOCKET,SOL_SOCKET,SO_TYPE,&type,&length) || type != SOCK_SEQPACKET ||
        fcntl(WORKER_SOCKET,F_SETFL,O_NONBLOCK) || setpriority(PRIO_PROCESS,0,0)) return 1;
    close_inherited();
    reader *s = calloc(1,sizeof *s);
    if (!s) return 1;
    s->directory = argv[2];
    for (unsigned n = 0; n < DEVICES; ++n) {
        s->devices[n].fd = -1; s->devices[n].probe_socket = -1;
    }
    uint64_t generation = 0; uint32_t total = 0; int active = 0, failed = 0;
    for (;;) {
        struct pollfd control = {WORKER_SOCKET,POLLIN,0};
        int ready = poll(&control,1,5);
        if (ready < 0) { if (errno == EINTR) continue; failed = 1; break; }
        if (control.revents & (POLLHUP|POLLERR|POLLNVAL)) break;
        int boundary = 0;
        for (unsigned n = 0; n < MESSAGES_PER_POLL; ++n) {
            message command;
            ssize_t bytes = recv(WORKER_SOCKET,&command,sizeof command,MSG_DONTWAIT|MSG_TRUNC);
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
            if (bytes != sizeof command || command.magic != COMMAND_MAGIC || command.value > 1 ||
                !command.generation || command.generation <= generation) { failed = 1; break; }
            generation = command.generation; active = (int)command.value;
            total = 0; boundary = 1;
        }
        if (failed) break;
        if (!generation) continue;
        if (boundary) for (unsigned n = 0; n < DEVICES; ++n) read_device(&s->devices[n],0);
        uint64_t now = milliseconds();
        for (unsigned n = 0; n < DEVICES; ++n) poll_probe(&s->devices[n],now);
        if (now >= s->next_scan) { scan(s); s->next_scan = now+250; }
        for (unsigned n = 0; n < DEVICES; ++n) {
            device *d = &s->devices[n];
            if (d->fd >= 0 && d->retryable && !d->probe_pid && now >= d->next_probe)
                start_probe(d,now);
        }
        for (unsigned n = 0; n < DEVICES; ++n) total += read_device(&s->devices[n],active);
        message response = {RESPONSE_MAGIC,total,generation};
        ssize_t bytes = send(WORKER_SOCKET,&response,sizeof response,MSG_DONTWAIT|MSG_NOSIGNAL);
        if (bytes != sizeof response && (bytes >= 0 ||
            (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))) { failed = 1; break; }
    }
    for (unsigned n = 0; n < DEVICES; ++n) drop(&s->devices[n]);
    free(s); close(WORKER_SOCKET); return failed;
}
