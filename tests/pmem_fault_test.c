#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/pmem.h"
#include "tic.h"
#include "api.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Linker wrappers exercise production pmem code with real files. Only its
 * mkstemp descriptor and exact parent directory receive injected faults. */
enum fault { NONE, SHORT_EINTR, WRITE_ERROR, WRITE_ZERO, FILE_SYNC,
             FILE_CLOSE, RENAME_ERROR, DIR_OPEN, DIR_SYNC, DIR_UNSUPPORTED,
             SLOW_SYNC, CREATE_ERROR, CRASH_BEFORE_RENAME, CRASH_AFTER_RENAME,
             DIR_CLOSE, FILE_SYNC_ONCE };
static enum fault fault;
static int active_fd = -1, write_calls, committed;
static int fault_syncs;
static uint32_t commits[8];
static char directory[256];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int entered, released;
static int crash_pipe = -1;
int __real_mkstemp(char *);
int __real_mkstemp64(char *);
ssize_t __real_write(int, const void *, size_t);
int __real_fsync(int);
int __real_close(int);
int __real_rename(const char *, const char *);
int __real_open(const char *, int, ...);
int __real_open64(const char *, int, ...);

static void crash_boundary(void)
{
    assert(__real_write(crash_pipe, "1", 1) == 1);
    for (;;) pause(); /* parent kills at the observed boundary, without cleanup */
}

int __wrap_mkstemp(char *path)
{
    if (fault == CREATE_ERROR) { errno = EACCES; return -1; }
    active_fd = __real_mkstemp(path);
    write_calls = 0;
    return active_fd;
}
int __wrap_mkstemp64(char *path)
{
    if (fault == CREATE_ERROR) { errno = EACCES; return -1; }
    active_fd = __real_mkstemp64(path);
    write_calls = 0;
    return active_fd;
}
ssize_t __wrap_write(int fd, const void *bytes, size_t count)
{
    if (fd == active_fd) {
        ++write_calls;
        if (fault == SHORT_EINTR) {
            if (write_calls == 1) { errno = EINTR; return -1; }
            if (count > 37) count = 37;
        }
        if (fault == WRITE_ERROR) {
            if (write_calls > 1) { errno = EIO; return -1; }
            if (count > 13) count = 13;
        }
        if (fault == WRITE_ZERO) { errno = ENOSPC; return 0; }
    }
    return __real_write(fd, bytes, count);
}
int __wrap_fsync(int fd)
{
    if (fd == active_fd) {
        if (fault == FILE_SYNC) { errno = EIO; return -1; }
        if (fault == FILE_SYNC_ONCE && fault_syncs++ == 0) { errno = EIO; return -1; }
        if (fault == SLOW_SYNC) {
            pthread_mutex_lock(&lock);
            entered = 1;
            pthread_cond_broadcast(&changed);
            while (!released) pthread_cond_wait(&changed, &lock);
            pthread_mutex_unlock(&lock);
        }
    } else {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (fault == DIR_SYNC) { errno = EIO; return -1; }
            if (fault == DIR_UNSUPPORTED) { errno = EINVAL; return -1; }
        }
    }
    return __real_fsync(fd);
}
int __wrap_close(int fd)
{
    int file = fd == active_fd;
    struct stat st;
    int dir = fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
    int result = __real_close(fd);
    if (file) active_fd = -1;
    if (file && fault == FILE_CLOSE) { errno = EIO; return -1; }
    if (dir && fault == DIR_CLOSE) { errno = EIO; return -1; }
    return result;
}
int __wrap_rename(const char *from, const char *to)
{
    if (fault == RENAME_ERROR) { errno = EIO; return -1; }
    if (fault == CRASH_BEFORE_RENAME) crash_boundary();
    int result = __real_rename(from, to);
    if (!result) {
        FILE *f = fopen(to, "rb");
        unsigned char bytes[16];
        assert(f && fread(bytes, 1, sizeof bytes, f) == sizeof bytes && fclose(f) == 0);
        assert(committed < 8);
        commits[committed++] = bytes[12] | (uint32_t)bytes[13] << 8 |
            (uint32_t)bytes[14] << 16 | (uint32_t)bytes[15] << 24;
    }
    if (!result && fault == CRASH_AFTER_RENAME) crash_boundary();
    return result;
}
int __wrap_open(const char *path, int flags, ...)
{
    if (fault == DIR_OPEN && !strcmp(path, directory)) { errno = EACCES; return -1; }
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode_t mode = (mode_t)va_arg(args, int);
        va_end(args);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}
int __wrap_open64(const char *path, int flags, ...)
{
    if (fault == DIR_OPEN && !strcmp(path, directory)) { errno = EACCES; return -1; }
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode_t mode = (mode_t)va_arg(args, int);
        va_end(args);
        return __real_open64(path, flags, mode);
    }
    return __real_open64(path, flags);
}
static uint32_t *values(tic80 *tic) { return ((tic_mem *)tic)->ram->persistent.data; }
static double now(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void no_temps(const char *path)
{
    char pattern[350];
    snprintf(pattern, sizeof pattern, "%s.tmp-*", path);
    glob_t paths = {0};
    assert(glob(pattern, 0, NULL, &paths) == GLOB_NOMATCH);
    globfree(&paths);
}
static void reset_fault(enum fault mode)
{
    fault = mode; active_fd = -1; committed = fault_syncs = 0;
    entered = released = 0;
}
static void saved_value(const char *path, uint32_t expected)
{
    tic80 *restored = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    assert(restored);
    tm_pmem save;
    assert(tm_pmem_open(&save, restored, path) == 0);
    assert(values(restored)[0] == expected && values(restored)[255] == expected);
    tm_pmem_close(&save);
    tic80_delete(restored);
}
static void set_values(tic80 *tic, uint32_t value)
{
    for (unsigned i = 0; i < 256; ++i) values(tic)[i] = value;
}
static void failure_case(tic80 *tic, tm_pmem *save, const char *path, enum fault mode)
{
    reset_fault(NONE);
    set_values(tic, 10);
    assert(tm_pmem_save(save, tic) == 0);
    reset_fault(mode);
    set_values(tic, 20);
    printf("Injecting persistent-memory failure %d\n", mode); fflush(stdout);
    assert(tm_pmem_save(save, tic) != 0);
    assert(save->saved[0] == 10);
    /* Pre-rename failures preserve the old file. Post-rename failures report
     * missing durability while the new complete, CRC-valid file is visible. */
    saved_value(path, mode == DIR_OPEN || mode == DIR_SYNC || mode == DIR_CLOSE ? 20 : 10);
    no_temps(path);
    reset_fault(NONE);
    assert(tm_pmem_save(save, tic) == 0);
    saved_value(path, 20);
}
static void crash_case(tic80 *tic, tm_pmem *save, const char *path, enum fault mode,
                       uint32_t before, uint32_t after)
{
    reset_fault(NONE);
    set_values(tic, before);
    assert(tm_pmem_save(save, tic) == 0);
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(pipefd[0]);
        crash_pipe = pipefd[1];
        reset_fault(mode);
        set_values(tic, after);
        tm_pmem_save(save, tic);
        _exit(2);
    }
    close(pipefd[1]);
    char observed;
    assert(read(pipefd[0], &observed, 1) == 1 && observed == '1');
    assert(kill(child, SIGKILL) == 0);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    close(pipefd[0]);
    saved_value(path, mode == CRASH_BEFORE_RENAME ? before : after);
    /* An abrupt pre-rename process death may leave its complete temporary file;
     * it cannot replace the committed save. Remove only this fixture's files. */
    char pattern[350];
    snprintf(pattern, sizeof pattern, "%s.tmp-*", path);
    glob_t temps = {0};
    int result = glob(pattern, 0, NULL, &temps);
    assert(result == 0 || result == GLOB_NOMATCH);
    assert(temps.gl_pathc == (mode == CRASH_BEFORE_RENAME ? 1u : 0u));
    for (size_t i = 0; i < temps.gl_pathc; ++i) assert(unlink(temps.gl_pathv[i]) == 0);
    globfree(&temps);
    reset_fault(NONE);
    set_values(tic, after + 1);
    assert(tm_pmem_save(save, tic) == 0);
    saved_value(path, after + 1);
}
int main(void)
{
    char root[] = "/tmp/tic80-pmem-fault-XXXXXX";
    assert(mkdtemp(root));
    snprintf(directory, sizeof directory, "%s", root);
    char path[300];
    snprintf(path, sizeof path, "%s/save.pmem", root);
    tic80 *tic = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    assert(tic);
    tm_pmem save;
    assert(tm_pmem_open(&save, tic, path) == 0);
    reset_fault(SHORT_EINTR);
    set_values(tic, 5);
    assert(tm_pmem_save(&save, tic) == 0 && write_calls > 20);
    saved_value(path, 5);
    const enum fault failures[] = { WRITE_ERROR, WRITE_ZERO, FILE_SYNC,
        FILE_CLOSE, RENAME_ERROR, DIR_OPEN, DIR_SYNC, DIR_CLOSE, CREATE_ERROR };
    for (unsigned i = 0; i < sizeof failures / sizeof *failures; ++i)
        failure_case(tic, &save, path, failures[i]);
    reset_fault(DIR_UNSUPPORTED);
    set_values(tic, 30);
    assert(tm_pmem_save(&save, tic) == 0);
    saved_value(path, 30);
    reset_fault(SLOW_SYNC);
    set_values(tic, 40);
    assert(tm_pmem_schedule(&save, tic) == 0);
    pthread_mutex_lock(&lock);
    while (!entered) pthread_cond_wait(&changed, &lock);
    pthread_mutex_unlock(&lock);
    assert(tm_pmem_status(&save) == 1); /* active I/O, before another queue */
    double start = now();
    for (uint32_t i = 41; i <= 140; ++i) {
        set_values(tic, i);
        assert(tm_pmem_schedule(&save, tic) == 0);
    }
    assert(now() - start < .1); /* blocked filesystem cannot block these ticks */
    assert(tm_pmem_status(&save) == 1);
    saved_value(path, 30);
    pthread_mutex_lock(&lock);
    released = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    assert(tm_pmem_save(&save, tic) == 0 && !save.worker);
    assert(tm_pmem_status(&save) == 0);
    assert(committed == 2 && commits[0] == 40 && commits[1] == 140);
    saved_value(path, 140);
    no_temps(path);
    reset_fault(FILE_SYNC);
    set_values(tic, 150);
    assert(tm_pmem_schedule(&save, tic) == 0);
    double deadline = now() + 1;
    while (tm_pmem_schedule(&save, tic) == 0) {
        assert(now() < deadline);
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    assert(tm_pmem_status(&save) == -1);
    assert(tm_pmem_save(&save, tic) != 0 && !save.worker);
    saved_value(path, 140);
    no_temps(path);
    reset_fault(NONE);
    assert(tm_pmem_save(&save, tic) == 0);
    saved_value(path, 150);
    reset_fault(FILE_SYNC_ONCE);
    set_values(tic, 200);
    assert(tm_pmem_schedule(&save, tic) == 0);
    deadline = now() + 1;
    while (tm_pmem_schedule(&save, tic) == 0) {
        assert(now() < deadline);
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    /* Retrying an unchanged snapshot must recover without resetting the game
     * or falling back to a synchronous save on the render thread. */
    deadline = now() + 1;
    while (tm_pmem_schedule(&save, tic) != 0) {
        assert(now() < deadline);
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    assert(save.worker);
    saved_value(path, 200);
    assert(tm_pmem_save(&save, tic) == 0 && committed == 1 && fault_syncs == 2);
    no_temps(path);
    crash_case(tic, &save, path, CRASH_BEFORE_RENAME, 160, 170);
    crash_case(tic, &save, path, CRASH_AFTER_RENAME, 180, 190);
    tm_pmem_close(&save);
    tic80_delete(tic);
    assert(unlink(path) == 0 && rmdir(root) == 0);
    puts("Persistent memory: interrupted/short writes, atomic failures, directory durability, retry, slow-worker coalescing, async errors, transient recovery, crash boundaries and final flush passed");
    return 0;
}
