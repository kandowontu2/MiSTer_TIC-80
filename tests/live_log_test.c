#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/live_log.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef struct { atomic_int entered,resume; unsigned writes; char text[4096]; size_t size; } sink;
static uint64_t now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
static ssize_t slow_write(void *argument,const char *data,size_t size) {
    sink *s=argument; atomic_store(&s->entered,1);
    while(!atomic_load(&s->resume)) {
        struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
    }
    assert(s->size+size<sizeof s->text);
    memcpy(s->text+s->size,data,size); s->size+=size; ++s->writes;
    return (ssize_t)size;
}
int main(void) {
    sink s={0}; cookie_io_functions_t callbacks={.write=slow_write};
    FILE *file=fopencookie(&s,"w",callbacks); assert(file);
    tm_live_log *log=tm_live_log_open(file); assert(log);
    tm_live_log_printf(log,"initial record\n");
    uint64_t limit=now()+2000000000ULL;
    while(!atomic_load(&s.entered)) { assert(now()<limit); usleep(1000); }
    uint64_t start=now();
    for(unsigned i=0;i<64;++i) tm_live_log_printf(log,"record %u\n",i);
    assert(now()-start<50000000ULL); // producer stays responsive with a blocked sink
    atomic_store(&s.resume,1);
    assert(tm_live_log_close(log)==32);
    assert(s.writes==33 && strstr(s.text,"initial record\nrecord 0\n"));
    assert(strstr(s.text,"record 31\n") && !strstr(s.text,"record 32\n"));
    assert(!fclose(file));
    puts("Blocked log sink cannot block the producer; bounded overflow is counted and retained records drain in order");
    return 0;
}
