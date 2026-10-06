#define _GNU_SOURCE
/* Native-only fixture: delay one exact private save read in a worker.
 * The control and arm files are supplied by the bounded hardware test. */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint64_t now(void) {
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)) _exit(2);
    return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;
}
static void delay_open(const char *path,const char *mode) {
    const char *prefix=getenv("TM_TEST_PMEM_READ_DIR"),*arm=getenv("TM_TEST_PMEM_READ_ARM");
    const char *entered=getenv("TM_TEST_PMEM_READ_ENTERED"),*control=getenv("TM_TEST_PMEM_READ_CONTROL");
    if(getpgrp()!=getpid() || !prefix || !arm || !entered || !control || strcmp(mode,"rb") ||
       strncmp(path,prefix,strlen(prefix))) return;
    int fd=open(control,O_RDONLY); if(fd<0) return;
    char bytes[1200]={0}; ssize_t size=read(fd,bytes,sizeof bytes-1); int closed=close(fd);
    if(size<=0 || closed) return;
    char *line=strchr(bytes,'\n'); if(!line) return; *line++=0;
    char *end; long ms=strtol(line,&end,10);
    if(strcmp(bytes,path) || (*end!='\n' && *end) || ms<1 || ms>30000 || unlink(arm)) return;
    uint64_t start=now(); char marker[1500];
    int n=snprintf(marker,sizeof marker,"{\"worker\":%ld,\"monotonic_ns\":%llu,\"delay_ms\":%ld,\"path\":\"%s\"}\n",
        (long)getpid(),(unsigned long long)start,ms,path);
    fd=open(entered,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(n<0 || n>=(int)sizeof marker || fd<0 || write(fd,marker,n)!=n || close(fd)) _exit(2);
    fprintf(stderr,"Injected native %ld ms save read delay: worker=%ld path=%s\n",ms,(long)getpid(),path); fflush(stderr);
    struct timespec delay={ms/1000,(ms%1000)*1000000},remaining;
    while(nanosleep(&delay,&remaining) && errno==EINTR) delay=remaining;
}
FILE *fopen(const char *path,const char *mode) {
    FILE *(*original)(const char *,const char *)=dlsym(RTLD_NEXT,"fopen");
    if(!original) { errno=EIO; return NULL; } delay_open(path,mode); return original(path,mode);
}
FILE *fopen64(const char *path,const char *mode) {
    FILE *(*original)(const char *,const char *)=dlsym(RTLD_NEXT,"fopen64");
    if(!original) { errno=EIO; return NULL; } delay_open(path,mode); return original(path,mode);
}
