#define _GNU_SOURCE
#include "tic80_mister/linux_wheel.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

enum { MAX_DEVICES = 64, EVENTS_PER_POLL = 512 };
typedef struct {
    int fd, seen;
    char name[32];
    dev_t device, special;
    ino_t inode;
    struct timespec changed;
    tm_wheel_device wheel;
} device;
struct tm_linux_wheel {
    char *directory;
    uint64_t next_scan;
    uint32_t total;
    unsigned epoch;
    int initialized, active;
    device devices[MAX_DEVICES];
};

int32_t tm_wheel_event(tm_wheel_device *s, const struct input_event *e, int active)
{
    if (!active) { s->pending = s->remainder = 0; s->dropping = 0; return 0; }
    if (e->type == EV_SYN && e->code == SYN_DROPPED) {
        s->pending = s->remainder = 0; s->dropping = 1; return 0;
    }
    if (s->dropping) {
        if (e->type == EV_SYN && e->code == SYN_REPORT) s->dropping = 0;
        return 0;
    }
    if (e->type == EV_REL && e->code == (s->high_resolution ? REL_HWHEEL_HI_RES : REL_HWHEEL)) {
        const int64_t limit = (int64_t)INT32_MAX * 120;
        s->pending += e->value;
        if (s->pending > limit) s->pending = limit;
        if (s->pending < -limit) s->pending = -limit;
    }
    if (e->type != EV_SYN || e->code != SYN_REPORT) return 0;
    int64_t units = s->pending + s->remainder;
    s->pending = 0;
    int64_t delta = s->high_resolution ? units / 120 : units;
    s->remainder = s->high_resolution ? units % 120 : 0;
    return delta > INT32_MAX ? INT32_MAX : delta < INT32_MIN ? INT32_MIN : (int32_t)delta;
}
static void drop(device *d)
{
    if (d->fd >= 0) close(d->fd);
    memset(d, 0, sizeof *d); d->fd = -1;
}
static int same(const device *d, const struct stat *s)
{
    return d->device == s->st_dev && d->special == s->st_rdev && d->inode == s->st_ino &&
        d->changed.tv_sec == s->st_ctim.tv_sec && d->changed.tv_nsec == s->st_ctim.tv_nsec;
}
static int event_name(const char *s)
{
    if (strncmp(s, "event", 5) || !s[5]) return 0;
    for (s += 5; *s; ++s) if (*s < '0' || *s > '9') return 0;
    return 1;
}
static void scan(tm_linux_wheel *s)
{
    DIR *dir = opendir(s->directory);
    if (!dir) {
        for (unsigned i = 0; i < MAX_DEVICES; ++i) drop(&s->devices[i]);
        return;
    }
    for (unsigned i = 0; i < MAX_DEVICES; ++i) s->devices[i].seen = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        struct stat metadata;
        if (!event_name(entry->d_name) || strlen(entry->d_name) >= sizeof s->devices[0].name ||
            fstatat(dirfd(dir), entry->d_name, &metadata, AT_SYMLINK_NOFOLLOW) || !S_ISCHR(metadata.st_mode)) continue;
        device *free_slot = NULL, *found = NULL;
        for (unsigned i = 0; i < MAX_DEVICES; ++i) {
            device *d = &s->devices[i];
            if (d->fd >= 0 && !strcmp(d->name, entry->d_name)) {
                if (same(d, &metadata)) found = d;
                else drop(d);
            }
            if (d->fd < 0 && !free_slot) free_slot = d;
        }
        if (found) { found->seen = 1; continue; }
        if (!free_slot) continue;
        int fd = openat(dirfd(dir), entry->d_name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) continue;
        struct stat opened;
        unsigned long bits[(REL_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];
        memset(bits, 0, sizeof bits);
        device *d = free_slot;
        d->device = metadata.st_dev; d->special = metadata.st_rdev; d->inode = metadata.st_ino; d->changed = metadata.st_ctim;
        if (fstat(fd, &opened) || !same(d, &opened) || ioctl(fd, EVIOCGBIT(EV_REL, sizeof bits), bits) < 0) { close(fd); continue; }
        const unsigned word = 8 * sizeof(unsigned long);
        int fine = !!(bits[REL_HWHEEL_HI_RES / word] & (1UL << (REL_HWHEEL_HI_RES % word)));
        int coarse = !!(bits[REL_HWHEEL / word] & (1UL << (REL_HWHEEL % word)));
        if (!fine && !coarse) { close(fd); continue; }
        d->fd = fd; d->seen = 1; strcpy(d->name, entry->d_name);
        d->wheel = (tm_wheel_device){.high_resolution = fine};
    }
    closedir(dir);
    for (unsigned i = 0; i < MAX_DEVICES; ++i) if (!s->devices[i].seen) drop(&s->devices[i]);
}
tm_linux_wheel *tm_linux_wheel_open(const char *directory)
{
    tm_linux_wheel *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (unsigned i = 0; i < MAX_DEVICES; ++i) s->devices[i].fd = -1;
    s->directory = strdup(directory ? directory : "/dev/input");
    if (!s->directory) { free(s); return NULL; }
    return s;
}
uint32_t tm_linux_wheel_poll(tm_linux_wheel *s, uint64_t now, int active, unsigned epoch)
{
    if (!s) return 0;
    int boundary = s->initialized && (s->active != !!active || s->epoch != epoch);
    s->initialized = 1; s->active = !!active; s->epoch = epoch;
    if (now >= s->next_scan) { scan(s); s->next_scan = now + 250; }
    for (unsigned i = 0; i < MAX_DEVICES; ++i) {
        device *d = &s->devices[i];
        if (d->fd < 0) continue;
        if (boundary || !active) d->wheel = (tm_wheel_device){.high_resolution = d->wheel.high_resolution};
        unsigned count = 0;
        while (count < EVENTS_PER_POLL) {
            struct input_event events[32];
            ssize_t bytes = read(d->fd, events, sizeof events);
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (bytes <= 0 || bytes % sizeof events[0]) { drop(d); break; }
            unsigned length = (unsigned)bytes / sizeof events[0]; count += length;
            for (unsigned n = 0; n < length; ++n) s->total += (uint32_t)tm_wheel_event(&d->wheel, &events[n], active && !boundary);
        }
        /* A mode transition with more queued events than this tick can drain
         * closes the stream: reopening starts a fresh kernel client queue. */
        if ((boundary || !active) && count >= EVENTS_PER_POLL) drop(d);
    }
    return s->total;
}
void tm_linux_wheel_close(tm_linux_wheel *s)
{
    if (!s) return;
    for (unsigned i = 0; i < MAX_DEVICES; ++i) drop(&s->devices[i]);
    free(s->directory); free(s);
}
