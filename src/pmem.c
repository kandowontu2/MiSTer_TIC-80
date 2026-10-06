#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/pmem.h"
#include "tic.h"
#include "api.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#include "zlib.h"
#include "ext/md5.h"
/* Pinned upstream adapter: libretro uses the same tic_mem prefix/ram field.
 * Keep private runtime access here rather than exposing it to the frontend. */
static uint32_t *values(tic80 *tic) { return ((tic_mem *)tic)->ram->persistent.data; }
void tm_pmem_key(tic80 *tic, char key[33])
{
    tic_mem *mem = (tic_mem *)tic;
    const void *bytes = &mem->cart.bank0;
    size_t length = sizeof mem->cart.bank0;
    if (*mem->saveid) { bytes = mem->saveid; length = strlen(mem->saveid); }
    MD5_CTX context;
    uint8_t digest[16];
    MD5_Init(&context);
    MD5_Update(&context, bytes, (unsigned long)length);
    MD5_Final(digest, &context);
    for (unsigned i = 0; i < 16; ++i) sprintf(key + i*2, "%02x", digest[i]);
    key[32] = 0;
}
struct tm_pmem_worker {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    tm_pmem *owner;
    uint32_t queued[256];
    int pending, busy, stopping, error;
};
static void stop_worker(tm_pmem *save);
static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}
static uint32_t get32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
int tm_pmem_open_values(tm_pmem *s, uint32_t current[256], const char *path)
{
    if (!s) return -1;
    memset(s, 0, sizeof *s);
    if (!current || !path || !*path || !(s->path = strdup(path))) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno != ENOENT) { perror(path); tm_pmem_close(s); return -1; }
        memcpy(s->saved, current, sizeof s->saved);
        return 0;
    }
    uint8_t bytes[1036];
    int valid = fread(bytes, 1, sizeof bytes, f) == sizeof bytes && fgetc(f) == EOF && !ferror(f);
    if (fclose(f)) valid = 0;
    if (!valid || memcmp(bytes, "TMPM", 4) || get32(bytes + 4) != 1 ||
            get32(bytes + 8) != crc32(0, bytes + 12, 1024)) {
        fprintf(stderr, "Invalid persistent memory file: %s\n", path);
        tm_pmem_close(s);
        return -1;
    }
    for (unsigned i = 0; i < 256; ++i) s->saved[i] = get32(bytes + 12 + i * 4);
    memcpy(current, s->saved, sizeof s->saved);
    return 0;
}
int tm_pmem_open(tm_pmem *s, tic80 *tic, const char *path)
{ return tm_pmem_open_values(s, tic ? values(tic) : NULL, path); }
static int save_values(tm_pmem *s, const uint32_t *current)
{
    if (!s->path || !current) return -1;
    if (!memcmp(s->saved, current, sizeof s->saved)) return 0;
    uint8_t bytes[1036];
    memcpy(bytes, "TMPM", 4);
    put32(bytes + 4, 1);
    for (unsigned i = 0; i < 256; ++i) put32(bytes + 12 + i * 4, current[i]);
    put32(bytes + 8, (uint32_t)crc32(0, bytes + 12, 1024));
    char *temp = malloc(strlen(s->path) + 16);
    if (!temp) return -1;
    sprintf(temp, "%s.tmp-XXXXXX", s->path);
    int fd = mkstemp(temp);
    int result = -1;
    if (fd < 0) { perror(temp); goto done; }
    size_t written = 0;
    while (written < sizeof bytes) {
        ssize_t n = write(fd, bytes + written, sizeof bytes - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += (size_t)n;
    }
    int flushed = written == sizeof bytes && fsync(fd) == 0;
    if (close(fd)) flushed = 0;
    if (!flushed || rename(temp, s->path)) { perror("Persistent memory save"); goto done; }
    /* Flush the rename as well as its contents where the filesystem supports it. */
    char *dir = strdup(s->path);
    if (!dir) goto done;
    char *slash = strrchr(dir, '/');
    if (slash) { if (slash == dir) slash[1] = 0; else *slash = 0; }
    else strcpy(dir, ".");
    int directory = open(dir, O_RDONLY | O_DIRECTORY);
    free(dir);
    if (directory < 0) { perror("Persistent memory directory"); goto done; }
    int synced = fsync(directory);
    int sync_errno = errno;
    int closed = close(directory);
    if (synced && sync_errno != EINVAL && sync_errno != EROFS) {
        errno = sync_errno;
        perror("Persistent memory directory sync");
        goto done;
    }
    if (closed) { perror("Persistent memory directory close"); goto done; }
    memcpy(s->saved, current, sizeof s->saved);
    result = 0;
done:
    if (result) unlink(temp);
    free(temp);
    return result;
}
static void *worker_loop(void *arg)
{
#ifdef __linux__
    /* Linux niceness is per thread. Keep disk saves at background priority
     * rather than inheriting the live playback thread's elevated priority. */
    if (setpriority(PRIO_PROCESS, 0, 19)) perror("Persistent memory worker priority");
#endif
    struct tm_pmem_worker *w = arg;
    pthread_mutex_lock(&w->mutex);
    for (;;) {
        while (!w->pending && !w->stopping) pthread_cond_wait(&w->changed, &w->mutex);
        if (!w->pending && w->stopping) break;
        uint32_t snapshot[256];
        memcpy(snapshot, w->queued, sizeof snapshot);
        w->pending = 0;
        w->busy = 1;
        pthread_mutex_unlock(&w->mutex);
        int error = save_values(w->owner, snapshot);
        pthread_mutex_lock(&w->mutex);
        w->error = error != 0;
        w->busy = 0;
    }
    pthread_mutex_unlock(&w->mutex);
    return NULL;
}
int tm_pmem_schedule_values(tm_pmem *s, const uint32_t current[256])
{
    if (!s || !s->path || !current) return -1;
    if (!s->worker) {
        struct tm_pmem_worker *w = calloc(1, sizeof *w);
        if (!w) return -1;
        w->owner = s;
        memcpy(w->queued, s->saved, sizeof w->queued);
        if (pthread_mutex_init(&w->mutex, NULL)) { free(w); return -1; }
        if (pthread_cond_init(&w->changed, NULL)) { pthread_mutex_destroy(&w->mutex); free(w); return -1; }
        if (pthread_create(&w->thread, NULL, worker_loop, w)) {
            pthread_cond_destroy(&w->changed); pthread_mutex_destroy(&w->mutex); free(w); return -1;
        }
        s->worker = w;
    }
    struct tm_pmem_worker *w = s->worker;
    pthread_mutex_lock(&w->mutex);
    int error = w->error;
    /* Report the last failure while retrying the newest snapshot in the
     * background. An unchanged snapshot also needs retry after failed I/O. */
    if (error || memcmp(w->queued, current, sizeof w->queued)) {
        memcpy(w->queued, current, sizeof w->queued);
        w->pending = 1;
        pthread_cond_signal(&w->changed);
    }
    pthread_mutex_unlock(&w->mutex);
    return error ? -1 : 0;
}
int tm_pmem_schedule(tm_pmem *s, tic80 *tic)
{ return tm_pmem_schedule_values(s, tic ? values(tic) : NULL); }
int tm_pmem_status(tm_pmem *s)
{
    if (!s || !s->path) return -1;
    struct tm_pmem_worker *w = s->worker;
    if (!w) return 0;
    pthread_mutex_lock(&w->mutex);
    int result = w->error ? -1 : (w->pending || w->busy ? 1 : 0);
    pthread_mutex_unlock(&w->mutex);
    return result;
}
static void stop_worker(tm_pmem *s)
{
    struct tm_pmem_worker *w = s->worker;
    if (!w) return;
    pthread_mutex_lock(&w->mutex);
    w->stopping = 1;
    pthread_cond_signal(&w->changed);
    pthread_mutex_unlock(&w->mutex);
    pthread_join(w->thread, NULL);
    pthread_cond_destroy(&w->changed);
    pthread_mutex_destroy(&w->mutex);
    free(w);
    s->worker = NULL;
}
int tm_pmem_save_values(tm_pmem *s, const uint32_t current[256])
{
    if (!s || !current) return -1;
    stop_worker(s);
    return save_values(s, current);
}
int tm_pmem_save(tm_pmem *s, tic80 *tic)
{ return tm_pmem_save_values(s, tic ? values(tic) : NULL); }
void tm_pmem_close(tm_pmem *s) { stop_worker(s); free(s->path); s->path = NULL; }
