#define _POSIX_C_SOURCE 200809L
/* Read-only DDR sampler. It never acquires transport ownership or writes a
 * session/control/payload word. The optional file mapping supports host tests. */
#include "tic80_mister/backend.h"
#include "tic80_mister/memory_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
static volatile sig_atomic_t stopping;
static void stop(int signo) { (void)signo; stopping=1; }
static uint64_t now(void)
{
    struct timespec time; clock_gettime(CLOCK_MONOTONIC,&time);
    return (uint64_t)time.tv_sec*1000000000u+time.tv_nsec;
}
static uint32_t reg(tm_backend *b,unsigned offset)
{
    uint32_t value=*(volatile uint32_t *)(b->memory+offset);
#if defined(__arm__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
    return value;
}
typedef struct {
    uint32_t nonce, slots, underruns, written, played, publication, presented, heartbeat;
} snapshot;
static int read_snapshot(tm_backend *b,snapshot *out)
{
    if (reg(b,TM_IDENTITY_OFFSET)!=TM_MAGIC || reg(b,TM_STATS_SEQUENCE_OFFSET+4)!=TM_STATS_MAGIC) return -1;
    for (unsigned attempt=0;attempt<64;++attempt) {
        uint32_t sequence=reg(b,TM_STATS_SEQUENCE_OFFSET);
        if (sequence&1) continue;
        snapshot next={0};
        next.nonce=reg(b,TM_SESSION_ACK_OFFSET);
        next.slots=reg(b,TM_AUDIO_STATS_OFFSET); next.underruns=reg(b,TM_AUDIO_STATS_OFFSET+4);
        next.written=reg(b,TM_AUDIO_WRITE_OFFSET); next.played=reg(b,TM_AUDIO_READ_OFFSET);
        next.publication=reg(b,TM_VIDEO_PUBLISH_OFFSET); next.presented=reg(b,TM_VIDEO_PRESENTED_OFFSET);
        next.heartbeat=reg(b,TM_HEARTBEAT_OFFSET);
        if (sequence!=reg(b,TM_STATS_SEQUENCE_OFFSET) || !next.nonce ||
            next.nonce!=reg(b,TM_SESSION_ACK_OFFSET) || next.nonce!=reg(b,TM_SESSION_REQUEST_OFFSET)) continue;
        if (reg(b,TM_IDENTITY_OFFSET)!=TM_MAGIC || reg(b,TM_STATS_SEQUENCE_OFFSET+4)!=TM_STATS_MAGIC) return -1;
        *out=next; return 1;
    }
    return 0;
}
static int selected(void)
{
    char name[80]={0}; FILE *file=fopen("/tmp/CORENAME","r"); if (!file) return 0;
    int read=fgets(name,sizeof name,file)!=NULL; fclose(file); name[strcspn(name,"\r\n")]=0;
    return read && !strcmp(name,"TIC-80");
}
static unsigned number(const char *text,unsigned low,unsigned high)
{
    char *end; errno=0; unsigned long value=strtoul(text,&end,10);
    if (errno || !*text || *end || value<low || value>high) return 0;
    return (unsigned)value;
}
int main(int argc,char **argv)
{
    const char *memory=NULL; unsigned seconds=600,interval=200;
    for (int arg=1;arg<argc;++arg) {
        if (!strcmp(argv[arg],"--memory") && arg+1<argc) memory=argv[++arg];
        else if (!strcmp(argv[arg],"--seconds") && arg+1<argc) seconds=number(argv[++arg],1,3600);
        else if (!strcmp(argv[arg],"--interval-ms") && arg+1<argc) interval=number(argv[++arg],10,5000);
        else { fprintf(stderr,"Usage: %s [--seconds 600] [--interval-ms 200] [--memory test-file]\n",argv[0]); return 2; }
    }
    if (!seconds || !interval || (!memory && !selected())) return 2;
    signal(SIGINT,stop); signal(SIGTERM,stop);
    tm_backend b; if (tm_backend_open(&b,memory)) return 1;
    uint64_t started=now(),deadline=started,last_snapshot=started,last_heartbeat=started;
    uint32_t nonce=0,heartbeat=0; int result=0;
    setvbuf(stdout,NULL,_IONBF,0);
    while (!stopping) {
        if (!memory && !selected()) { result=1; break; }
        snapshot state; int status=read_snapshot(&b,&state);
        if (status<0) { fprintf(stderr,"Matching hardware statistics unavailable\n"); result=1; break; }
        if (status>0) {
            if (nonce && nonce!=state.nonce) { fprintf(stderr,"Audio session changed during monitoring\n"); result=1; break; }
            nonce=state.nonce;
            last_snapshot=now();
            if (heartbeat!=state.heartbeat) { heartbeat=state.heartbeat; last_heartbeat=last_snapshot; }
            printf("{\"elapsed_ns\":%llu,\"session\":%u,\"slots\":%u,\"underruns\":%u,\"written\":%u,\"played\":%u,\"publication\":%u,\"presented\":%u,\"heartbeat\":%u}\n",
                (unsigned long long)(now()-started),state.nonce,state.slots,state.underruns,state.written,state.played,
                state.publication,state.presented,state.heartbeat);
        }
        if (now()-started>=(uint64_t)seconds*1000000000u) break;
        if (now()-last_snapshot>1000000000u) { fprintf(stderr,"No coherent active statistics snapshot\n"); result=1; break; }
        if (now()-last_heartbeat>1000000000u) { fprintf(stderr,"FPGA heartbeat stopped during monitoring\n"); result=1; break; }
        deadline+=(uint64_t)interval*1000000u;
        uint64_t current=now();
        if (deadline>current) {
            struct timespec wait={(time_t)(deadline/1000000000u),(long)(deadline%1000000000u)};
            while (clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&wait,NULL)==EINTR && !stopping) {}
        } else deadline=current;
    }
    tm_backend_close(&b); return result;
}
