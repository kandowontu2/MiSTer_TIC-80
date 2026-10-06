#define _POSIX_C_SOURCE 200809L
/* Read-only bounded audio journal across same-core FPGA sessions. */
#include "tic80_mister/memory_map.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
static uint64_t now(void) {
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;
}
static uint32_t reg(volatile const uint8_t *p,unsigned offset) {
    uint32_t value=*(volatile const uint32_t *)(p+offset);
    __asm__ volatile("dmb sy" ::: "memory"); return value;
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    char *end; unsigned seconds=strtoul(argv[1],&end,10);
    if(!*argv[1] || *end || seconds<1 || seconds>60) return 2;
    FILE *journal=fopen(argv[2],"wx"); if(!journal) return 1;
    int fd=open("/dev/mem",O_RDONLY|O_SYNC); if(fd<0) {fclose(journal); return 1;}
    volatile const uint8_t *p=mmap(NULL,TM_REGION_BYTES,PROT_READ,MAP_SHARED,fd,TM_PHYSICAL_BASE);
    if(p==MAP_FAILED) {close(fd); fclose(journal); return 1;}
    uint64_t start=now(),empty_since=0,last_flush=start;
    int seen_tic=0;
    do {
        char core[80]={0}; FILE *name=fopen("/tmp/CORENAME","r");
        if(!name) break;
        int read=fgets(core,sizeof core,name)!=NULL; fclose(name); core[strcspn(core,"\r\n")]=0;
        uint64_t time=now();
        if(!read || !*core) {if(!empty_since) empty_since=time; if(time-empty_since>1000000000ULL) break;}
        else {
            empty_since=0;
            if(!seen_tic && !strcmp(core,"MENU") && time-start<5000000000ULL) {
                struct timespec delay={0,5000000}; nanosleep(&delay,NULL); continue;
            }
            if(strcmp(core,"TIC-80")) break;
            seen_tic=1;
        }
        uint32_t seq=reg(p,TM_STATS_SEQUENCE_OFFSET),session=reg(p,TM_SESSION_ACK_OFFSET);
        uint32_t slots=reg(p,TM_AUDIO_STATS_OFFSET),under=reg(p,TM_AUDIO_STATS_OFFSET+4);
        uint32_t written=reg(p,TM_AUDIO_WRITE_OFFSET),played=reg(p,TM_AUDIO_READ_OFFSET);
        uint32_t request=reg(p,TM_SESSION_REQUEST_OFFSET);
        int coherent=!(seq&1) && session && session==request && session==reg(p,TM_SESSION_ACK_OFFSET) &&
            seq==reg(p,TM_STATS_SEQUENCE_OFFSET) && reg(p,TM_IDENTITY_OFFSET)==TM_MAGIC &&
            reg(p,TM_STATS_SEQUENCE_OFFSET+4)==TM_STATS_MAGIC;
        fprintf(journal,"{\"ns\":%llu,\"session\":%u,\"request\":%u,\"coherent_audio\":%d,\"slots\":%u,\"underruns\":%u,\"written\":%u,\"played\":%u,\"status\":%u,\"cart\":%u,\"ack\":%u,\"selected\":%d}\n",
            (unsigned long long)(time-start),session,request,coherent,slots,under,written,played,
            reg(p,TM_STATUS_OFFSET),reg(p,TM_CART_META_OFFSET),reg(p,TM_CART_ACK_OFFSET),read && !strcmp(core,"TIC-80"));
        if(time-last_flush>1000000000ULL) {if(fflush(journal)) break; last_flush=time;}
        struct timespec delay={0,5000000}; nanosleep(&delay,NULL);
    } while(now()-start<seconds*1000000000ULL);
    munmap((void*)p,TM_REGION_BYTES); close(fd); return fclose(journal)!=0;
}
