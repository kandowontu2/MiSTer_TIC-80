#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/* A worker consumes the fixture's one-shot arm file before delaying a checked
 * save open. Parent identity preparation and later retries remain unaffected. */
static void delay_open(const char *path,const char *mode)
{
    const char *prefix=getenv("TM_TEST_PMEM_READ_DIR"),*arm=getenv("TM_TEST_PMEM_READ_ARM");
    const char *entered=getenv("TM_TEST_PMEM_READ_ENTERED"),*duration=getenv("TM_TEST_PMEM_READ_MS");
    size_t length=strlen(path);
    if(getpgrp()!=getpid() || !prefix || !arm || !entered || !duration || strcmp(mode,"rb") ||
        strncmp(path,prefix,strlen(prefix)) || length<5 || strcmp(path+length-5,".pmem") || unlink(arm)) return;
    char *end; long ms=strtol(duration,&end,10);
    if(*end || ms<1 || ms>30000) _exit(2);
    char marker[64]; int bytes=snprintf(marker,sizeof marker,"%ld\n",(long)getpid());
    int fd=open(entered,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd<0 || write(fd,marker,bytes)!=bytes || close(fd)) _exit(2);
    fprintf(stderr,"Injected %ld ms Studio save read delay: worker=%ld\n",ms,(long)getpid()); fflush(stderr);
    struct timespec delay={ms/1000,(ms%1000)*1000000},remaining;
    while(nanosleep(&delay,&remaining) && errno==EINTR) delay=remaining;
}
FILE *fopen(const char *path,const char *mode)
{
    FILE *(*original)(const char *,const char *)=dlsym(RTLD_NEXT,"fopen");
    if(!original) { errno=EIO; return NULL; }
    delay_open(path,mode); return original(path,mode);
}
FILE *fopen64(const char *path,const char *mode)
{
    FILE *(*original)(const char *,const char *)=dlsym(RTLD_NEXT,"fopen64");
    if(!original) { errno=EIO; return NULL; }
    delay_open(path,mode); return original(path,mode);
}
