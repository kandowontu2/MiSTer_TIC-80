#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/* Delay one checked save-file flush; an optional fixture sentinel keeps writes
 * failing until the paced test removes it to model storage recovery. */
int fsync(int fd)
{
    static int delayed;
    int (*original)(int)=dlsym(RTLD_NEXT,"fsync");
    if(!original) { errno=EIO; return -1; }
    const char *prefix=getenv("TM_TEST_PMEM_SLOW_DIR");
    if(prefix && *prefix) {
        char descriptor[64],path[1024]; snprintf(descriptor,sizeof descriptor,"/proc/self/fd/%d",fd);
        ssize_t size=readlink(descriptor,path,sizeof path-1);
        if(size>=0) {
            path[size]=0;
            if(!strncmp(path,prefix,strlen(prefix)) && strstr(path,".pmem.tmp-") &&
                __sync_bool_compare_and_swap(&delayed,0,1)) {
                fputs("Injected 500 ms persistent-memory fsync delay\n",stderr);
                struct timespec delay={0,500000000},remaining;
                while(nanosleep(&delay,&remaining) && errno==EINTR) delay=remaining;
            }
            const char *failure=getenv("TM_TEST_PMEM_FAIL_FILE");
            if(!strncmp(path,prefix,strlen(prefix)) && strstr(path,".pmem.tmp-") &&
                failure && access(failure,F_OK)==0) { errno=EIO; return -1; }
        }
    }
    return original(fd);
}
