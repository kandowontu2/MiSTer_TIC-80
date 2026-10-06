#define _GNU_SOURCE
#include "tic80_mister/spawn_private.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __GLIBC__
#if __GLIBC_PREREQ(2,34) && !defined(TM_SPAWN_FORCE_FORK)
#define TM_NATIVE_SPAWN 1
#endif
#endif

static int valid(const tm_spawn_request *r)
{
    if(!r || !r->file || !*r->file || !r->argv || !r->argv[0] ||
       !r->envp || r->close_from<3 || r->close_from>6 ||
       r->count>3 || (r->count && !r->fds)) return EINVAL;
    for(size_t i=0;i<r->count;++i) {
        if(r->fds[i].source<r->close_from || r->fds[i].target<0 ||
           r->fds[i].target>=r->close_from) return EINVAL;
        if(fcntl(r->fds[i].source,F_GETFD)<0) return errno;
        for(size_t j=0;j<i;++j) if(r->fds[i].target==r->fds[j].target) return EINVAL;
    }
    return 0;
}

#ifdef TM_NATIVE_SPAWN
int tm_spawn_closed(pid_t *pid,const tm_spawn_request *r)
{
    int code=pid?valid(r):EINVAL;
    if(code) return code;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    code=posix_spawn_file_actions_init(&actions);
    if(code) return code;
    code=posix_spawnattr_init(&attr);
    if(code) { posix_spawn_file_actions_destroy(&actions); return code; }
    if(r->null_input) code=posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0);
    if(!code && r->null_error) code=posix_spawn_file_actions_addopen(&actions,2,"/dev/null",O_WRONLY,0);
    for(size_t i=0;!code && i<r->count;++i)
        code=posix_spawn_file_actions_adddup2(&actions,r->fds[i].source,r->fds[i].target);
    if(!code) code=posix_spawn_file_actions_addclosefrom_np(&actions,r->close_from);
    if(!code && r->process_group) code=posix_spawnattr_setpgroup(&attr,0);
    if(!code && r->process_group) code=posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);
    pid_t child=0;
    if(!code) code=r->search_path
        ?posix_spawnp(&child,r->file,&actions,&attr,r->argv,r->envp)
        :posix_spawn(&child,r->file,&actions,&attr,r->argv,r->envp);
    posix_spawnattr_destroy(&attr); posix_spawn_file_actions_destroy(&actions);
    if(!code) *pid=child;
    return code;
}
#else
/* Prepare PATH candidates before fork. The child performs only signal-safe
 * descriptor/signal operations and execve, with no allocation or stdio. */
typedef struct { char **items; size_t count; bool search; } paths;
static void free_paths(paths *p)
{
    for(size_t i=0;i<p->count;++i) free(p->items[i]);
    free(p->items);
}
static int make_paths(const tm_spawn_request *r,paths *p)
{
    p->search=r->search_path && !strchr(r->file,'/');
    const char *path=p->search?getenv("PATH"):NULL;
    if(p->search && !path) path="/bin:/usr/bin";
    size_t count=1;
    if(path) for(const char *s=path;*s;++s) if(*s==':') ++count;
    p->items=calloc(count,sizeof *p->items);
    if(!p->items) return ENOMEM;
    if(!path) {
        p->items[0]=strdup(r->file);
        if(!p->items[0]) { free_paths(p); return ENOMEM; }
        p->count=1; return 0;
    }
    size_t file_size=strlen(r->file);
    for(const char *at=path;;) {
        const char *end=strchr(at,':'); size_t length=end?(size_t)(end-at):strlen(at);
        char *candidate=malloc(length+file_size+2);
        if(!candidate) { free_paths(p); return ENOMEM; }
        memcpy(candidate,at,length);
        if(length) candidate[length++]='/';
        memcpy(candidate+length,r->file,file_size+1);
        p->items[p->count++]=candidate;
        if(!end) break;
        at=end+1;
    }
    return 0;
}

static int range(unsigned first,unsigned last)
{
#ifdef TM_SPAWN_FORCE_PROC_CLOSE
    (void)first; (void)last; errno=ENOSYS; return -1;
#elif defined(SYS_close_range)
    return (int)syscall(SYS_close_range,first,last,0);
#elif defined(__arm__)
    /* The SDK predates Linux 5.9's ARM EABI syscall definition. */
    return (int)syscall(436,first,last,0);
#else
    (void)first; (void)last; errno=ENOSYS; return -1;
#endif
}
struct linux_dirent64 {
    uint64_t ino;
    int64_t offset;
    unsigned short length;
    unsigned char type;
    char name[];
};
static int close_from(int first,int keep)
{
    if(!range((unsigned)first,(unsigned)keep-1) &&
       !range((unsigned)keep+1,UINT_MAX)) return 0;
    /* On older kernels enumerate in the forked child's private table, so
     * concurrent opens in parent threads cannot race the closure. */
    int directory=open("/proc/self/fd",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(directory<0) return -1;
    uint64_t storage[512]; int code=0;
    for(;;) {
        long size=syscall(SYS_getdents64,directory,storage,sizeof storage);
        if(size<0) { if(errno==EINTR) continue; code=errno; break; }
        if(!size) break;
        for(size_t at=0;at<(size_t)size;) {
            struct linux_dirent64 *entry=(void*)((char*)storage+at);
            size_t header=offsetof(struct linux_dirent64,name);
            if((size_t)size-at<header+1 || entry->length<header+2 ||
               entry->length>(size_t)size-at || entry->length%sizeof(uint64_t)) { code=EIO; break; }
            int fd=0; bool number=true,ended=false;
            for(size_t i=header;i<entry->length;++i) {
                unsigned char ch=((unsigned char*)entry)[i];
                if(!ch) { ended=true; break; }
                if(ch<'0' || ch>'9' || fd>(INT_MAX-(ch-'0'))/10) { number=false; break; }
                fd=fd*10+(ch-'0');
            }
            if(number && ended && fd>=first && fd!=keep && fd!=directory) close(fd);
            at+=entry->length;
        }
        if(code) break;
    }
    close(directory);
    if(code) { errno=code; return -1; }
    return 0;
}
static int null_fd(int target,int flags)
{
    int fd=open("/dev/null",flags);
    if(fd<0) return -1;
    if(fd==target) return 0;
    int result=dup2(fd,target),code=errno;
    close(fd); errno=code;
    return result<0?-1:0;
}
static void child_error(int fd,int code)
{
    const char *bytes=(const char*)&code; size_t left=sizeof code;
    while(left) {
        ssize_t size=write(fd,bytes,left);
        if(size>0) { bytes+=size; left-=(size_t)size; }
        else if(size<0 && errno==EINTR) continue;
        else break;
    }
    _exit(127);
}
int tm_spawn_closed(pid_t *pid,const tm_spawn_request *r)
{
    int code=pid?valid(r):EINVAL;
    if(code) return code;
    paths candidates={0}; code=make_paths(r,&candidates);
    if(code) return code;
    int pair[2];
    if(pipe2(pair,O_CLOEXEC)) { code=errno; free_paths(&candidates); return code; }
    int error_fd=fcntl(pair[1],F_DUPFD_CLOEXEC,64); code=errno;
    close(pair[1]);
    if(error_fd<0) { close(pair[0]); free_paths(&candidates); return code; }
    sigset_t all,original; sigfillset(&all);
    code=pthread_sigmask(SIG_SETMASK,&all,&original);
    if(code) { close(pair[0]); close(error_fd); free_paths(&candidates); return code; }
    pid_t child=fork(); code=errno;
    if(!child) {
        close(pair[0]);
        for(int signal=1;signal<NSIG;++signal) {
            struct sigaction old;
            if(!sigaction(signal,NULL,&old) && old.sa_handler!=SIG_DFL && old.sa_handler!=SIG_IGN) {
                struct sigaction reset={.sa_handler=SIG_DFL};
                if(sigaction(signal,&reset,NULL)) child_error(error_fd,errno);
            }
        }
        if(r->process_group && setpgid(0,0)) child_error(error_fd,errno);
        if(r->null_input && null_fd(0,O_RDONLY)) child_error(error_fd,errno);
        if(r->null_error && null_fd(2,O_WRONLY)) child_error(error_fd,errno);
        for(size_t i=0;i<r->count;++i)
            if(dup2(r->fds[i].source,r->fds[i].target)<0) child_error(error_fd,errno);
        if(close_from(r->close_from,error_fd)) child_error(error_fd,errno);
        if(sigprocmask(SIG_SETMASK,&original,NULL)) child_error(error_fd,errno);
        bool denied=false;
        for(size_t i=0;i<candidates.count;++i) {
            execve(candidates.items[i],r->argv,r->envp);
            if(!candidates.search) child_error(error_fd,errno);
            switch(errno) {
                case EACCES: denied=true; break;
                case ENOENT: case ENOTDIR: case ESTALE: case ENODEV: case ETIMEDOUT: break;
                default: child_error(error_fd,errno);
            }
        }
        child_error(error_fd,denied?EACCES:ENOENT);
    }
    pthread_sigmask(SIG_SETMASK,&original,NULL);
    close(error_fd); free_paths(&candidates);
    if(child<0) { close(pair[0]); return code; }
    size_t received=0; code=0; bool read_error=false;
    while(received<sizeof code) {
        ssize_t size=read(pair[0],(char*)&code+received,sizeof code-received);
        if(size>0) received+=(size_t)size;
        else if(!size) break;
        else if(errno!=EINTR) { code=errno; received=sizeof code; read_error=true; break; }
    }
    close(pair[0]);
    if(received) {
        if(received!=sizeof code || !code) code=EIO;
        if(read_error || received!=sizeof code) kill(child,SIGKILL);
        while(waitpid(child,NULL,0)<0 && errno==EINTR) {}
        return code;
    }
    *pid=child;
    return 0;
}
#endif
