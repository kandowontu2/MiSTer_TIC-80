#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/live_log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
enum { SLOTS=32, BYTES=512 };
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"Playback logging requires lock-free counters");
struct tm_live_log {
    FILE *sink;
    pthread_t worker;
    atomic_uint head,tail,dropped;
    atomic_int stopping;
    size_t lengths[SLOTS];
    char records[SLOTS][BYTES];
};
static void *write_log(void *argument) {
    tm_live_log *log=argument;
    if(setpriority(PRIO_PROCESS,0,19)) perror("Studio log priority");
    for(;;) {
        unsigned tail=atomic_load_explicit(&log->tail,memory_order_relaxed);
        unsigned head=atomic_load_explicit(&log->head,memory_order_acquire);
        if(tail==head) {
            if(atomic_load_explicit(&log->stopping,memory_order_acquire)) {
                if(tail==atomic_load_explicit(&log->head,memory_order_acquire)) break;
                continue;
            }
            struct timespec delay={0,10000000}; nanosleep(&delay,NULL); continue;
        }
        char record[BYTES]; size_t length=log->lengths[tail%SLOTS];
        memcpy(record,log->records[tail%SLOTS],length);
        // Release the slot before entering stdio: storage may stall here.
        atomic_store_explicit(&log->tail,tail+1,memory_order_release);
        if(fwrite(record,1,length,log->sink)!=length || fflush(log->sink))
            atomic_fetch_add_explicit(&log->dropped,1,memory_order_relaxed);
    }
    return NULL;
}
tm_live_log *tm_live_log_open(FILE *sink) {
    if(!sink) return NULL;
    tm_live_log *log=calloc(1,sizeof *log); if(!log) return NULL;
    log->sink=sink;
    atomic_init(&log->head,0); atomic_init(&log->tail,0);
    atomic_init(&log->dropped,0); atomic_init(&log->stopping,0);
    if(pthread_create(&log->worker,NULL,write_log,log)) { free(log); return NULL; }
    return log;
}
void tm_live_log_printf(tm_live_log *log,const char *format,...) {
    if(!log || !format) return;
    unsigned head=atomic_load_explicit(&log->head,memory_order_relaxed);
    unsigned tail=atomic_load_explicit(&log->tail,memory_order_acquire);
    if(head-tail>=SLOTS) {
        atomic_fetch_add_explicit(&log->dropped,1,memory_order_relaxed); return;
    }
    va_list args; va_start(args,format);
    int length=vsnprintf(log->records[head%SLOTS],BYTES,format,args); va_end(args);
    if(length<0 || length>=BYTES) {
        atomic_fetch_add_explicit(&log->dropped,1,memory_order_relaxed); return;
    }
    log->lengths[head%SLOTS]=(size_t)length;
    atomic_store_explicit(&log->head,head+1,memory_order_release);
}
unsigned tm_live_log_close(tm_live_log *log) {
    if(!log) return 0;
    atomic_store_explicit(&log->stopping,1,memory_order_release);
    pthread_join(log->worker,NULL);
    unsigned dropped=atomic_load_explicit(&log->dropped,memory_order_relaxed);
    free(log); return dropped;
}
