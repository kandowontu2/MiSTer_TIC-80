#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/studio_session.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc,char **argv) {
    if(argc>=2 && !strcmp(argv[1],"--studio-worker"))
        return tm_studio_session_worker(argc,argv);
    if(argc!=2 && !(argc==3 && !strcmp(argv[2],"--strong"))) return 2;
    int producer=argc==3?-20:-10, worker=producer/2;
    pid_t positive=fork(); assert(positive>=0);
    if(!positive) {
        assert(!setpriority(PRIO_PROCESS,0,3));
        tm_studio_session *normal=NULL;
        assert(tm_studio_session_open(&normal,argv[1])==TM_STUDIO_OK);
        assert(getpriority(PRIO_PROCESS,tm_studio_session_pid(normal))==3);
        assert(tm_studio_session_begin_pause(normal)==TM_STUDIO_OK);
        int result; do result=tm_studio_session_poll(normal,10); while(result==TM_STUDIO_PENDING);
        assert(result==TM_STUDIO_RECOVERED);
        assert(getpriority(PRIO_PROCESS,tm_studio_session_pid(normal))==3);
        assert(tm_studio_session_close(normal)==TM_STUDIO_OK); _exit(0);
    }
    int status; assert(waitpid(positive,&status,0)==positive && WIFEXITED(status) && !WEXITSTATUS(status));
    if(setpriority(PRIO_PROCESS,0,producer)) {
        if(errno==EPERM || errno==EACCES) return 77;
        perror("Playback test priority"); return 1;
    }
    tm_studio_session *studio=NULL;
    assert(tm_studio_session_open(&studio,argv[1])==TM_STUDIO_OK);
    assert(getpriority(PRIO_PROCESS,0)==producer);
    assert(getpriority(PRIO_PROCESS,tm_studio_session_pid(studio))==worker);
    for(unsigned n=0;n<3;++n) {
        long old=tm_studio_session_pid(studio);
        assert(tm_studio_session_begin_pause(studio)==TM_STUDIO_OK);
        int result;
        do result=tm_studio_session_poll(studio,10); while(result==TM_STUDIO_PENDING);
        assert(result==TM_STUDIO_RECOVERED);
        assert(tm_studio_session_pid(studio)!=old);
        assert(getpriority(PRIO_PROCESS,0)==producer);
        assert(getpriority(PRIO_PROCESS,tm_studio_session_pid(studio))==worker);
        DIR *tasks=opendir("/proc/self/task"); assert(tasks);
        struct dirent *task; unsigned helpers=0;
        while((task=readdir(tasks))) {
            pid_t tid=(pid_t)strtol(task->d_name,NULL,10);
            if(tid>0 && tid!=getpid()) { ++helpers; assert(getpriority(PRIO_PROCESS,tid)==0); }
        }
        closedir(tasks); assert(helpers==1);
    }
    assert(tm_studio_session_close(studio)==TM_STUDIO_OK);
    printf("Studio priorities: producer %d, initial and three replacement workers %d, recovery helper 0; positive priority preserved\n",producer,worker);
    return 0;
}
