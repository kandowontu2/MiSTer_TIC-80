#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/studio_session.h"
#include <assert.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void pinned(pid_t pid,int cpu) {
    cpu_set_t actual; CPU_ZERO(&actual);
    assert(!sched_getaffinity(pid,sizeof actual,&actual));
    assert(CPU_COUNT(&actual)==1 && CPU_ISSET(cpu,&actual));
}
int main(int argc,char **argv) {
    if(argc>=2 && !strcmp(argv[1],"--studio-worker"))
        return tm_studio_session_worker(argc,argv);
    if(argc!=2) return 2;
    cpu_set_t original; assert(!sched_getaffinity(0,sizeof original,&original));
    int worker=-1,producer=-1;
    for(int cpu=0;cpu<CPU_SETSIZE;++cpu) if(CPU_ISSET(cpu,&original)) {
        if(worker<0) worker=cpu; else { producer=cpu; break; }
    }
    if(producer<0) return 77;
    cpu_set_t placement; CPU_ZERO(&placement); CPU_SET(producer,&placement);
    assert(!sched_setaffinity(0,sizeof placement,&placement));
    tm_studio_session *studio=NULL;
    assert(tm_studio_session_open_on_cpu(&studio,argv[1],NULL,NULL,-2)==TM_STUDIO_ERROR && !studio);
    assert(tm_studio_session_open_on_cpu(&studio,argv[1],NULL,NULL,CPU_SETSIZE)==TM_STUDIO_ERROR && !studio);
    assert(tm_studio_session_open_on_cpu(&studio,argv[1],NULL,NULL,worker)==TM_STUDIO_OK);
    pinned(0,producer); pinned(tm_studio_session_pid(studio),worker);
    for(unsigned n=0;n<3;++n) {
        pid_t previous=tm_studio_session_pid(studio);
        assert(tm_studio_session_begin_pause(studio)==TM_STUDIO_OK);
        int result; do result=tm_studio_session_poll(studio,10); while(result==TM_STUDIO_PENDING);
        assert(result==TM_STUDIO_RECOVERED && tm_studio_session_pid(studio)!=previous);
        pinned(0,producer); pinned(tm_studio_session_pid(studio),worker);
    }
    assert(tm_studio_session_close(studio)==TM_STUDIO_OK);
    assert(tm_studio_session_open(&studio,argv[1])==TM_STUDIO_OK);
    pinned(0,producer); pinned(tm_studio_session_pid(studio),producer);
    assert(tm_studio_session_close(studio)==TM_STUDIO_OK);
    assert(!sched_setaffinity(0,sizeof original,&original));
    puts("Independent worker affinity survives three replacements; default inheritance and frontend affinity preserved");
    return 0;
}
