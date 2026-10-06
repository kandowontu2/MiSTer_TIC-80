#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
/* Single-dispatch test instrument. DDR is mapped read-only; a pidfd binds the
 * only signal to the explicitly identified Main. Never retries a signal. */
#include "tic80_mister/memory_map.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int signo) { (void)signo; stopping=1; }
static uint64_t now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock"); exit(1); }
    return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
}
static uint64_t number(const char *s,uint64_t maximum) {
    char *end; errno=0;
    unsigned long long value=strtoull(s,&end,10);
    return errno || !*s || *s=='-' || *end || !value || value>maximum ? 0 : value;
}
static int selected(const char *path) {
    char text[80]; FILE *f=fopen(path,"rb"); if(!f) return 0;
    size_t n=fread(text,1,sizeof text,f); int failed=ferror(f); fclose(f);
    if(failed || !n || n==sizeof text || memchr(text,0,n)) return 0;
    while(n && (text[n-1]=='\r' || text[n-1]=='\n')) --n;
    return n==6 && !memcmp(text,"TIC-80",6);
}
static int starttime(unsigned pid,uint64_t expected) {
    char path[80],line[4096]; snprintf(path,sizeof path,"/proc/%u/stat",pid);
    FILE *f=fopen(path,"r"); if(!f) return 0;
    int valid=fgets(line,sizeof line,f)!=NULL; fclose(f);
    char *end=valid?strrchr(line,')'):NULL;
    if(!end || end[1]!=' ' || end[2]=='Z' || end[2]=='X') return 0;
    char *state=NULL,*word=strtok_r(end+2," \n",&state);
    for(unsigned field=0;word;word=strtok_r(NULL," \n",&state),++field)
        if(field==19) return number(word,UINT64_MAX)==expected;
    return 0;
}
static int executable(unsigned pid,const char *expected,struct stat *metadata) {
    char path[80],name[PATH_MAX]; snprintf(path,sizeof path,"/proc/%u/exe",pid);
    ssize_t n=readlink(path,name,sizeof name-1);
    if(n<0 || n>=(ssize_t)sizeof name-1) return -1;
    name[n]=0; if(strcmp(name,expected)) return -1;
    int fd=open(path,O_RDONLY|O_CLOEXEC);
    if(fd<0) return -1;
    if(fstat(fd,metadata) || !S_ISREG(metadata->st_mode) || metadata->st_size<=0 ||
       metadata->st_size>64*1024*1024) { close(fd); return -1; }
    return fd;
}
static int same_file(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int digest(int fd,const char *expected) {
    int pipefd[2]; if(pipe2(pipefd,O_CLOEXEC)) return 0;
    pid_t child=fork();
    if(child==0) {
        if(dup2(fd,STDIN_FILENO)<0 || dup2(pipefd[1],STDOUT_FILENO)<0) _exit(126);
        close(pipefd[0]); close(pipefd[1]);
        execl("/usr/bin/sha256sum","sha256sum","-",(char *)NULL);
        if(errno==ENOENT) execl("/bin/sha256sum","sha256sum","-",(char *)NULL);
        _exit(127);
    }
    close(pipefd[1]); if(child<0) { close(pipefd[0]); return 0; }
    char text[80]; size_t n=0;
    while(n<sizeof text) {
        ssize_t count=read(pipefd[0],text+n,sizeof text-n);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) break;
        n+=(size_t)count;
    }
    close(pipefd[0]); int status;
    while(waitpid(child,&status,0)<0) { if(errno!=EINTR) return 0; }
    return WIFEXITED(status) && WEXITSTATUS(status)==0 && n>=66 && text[64]==' ' && !memcmp(text,expected,64);
}
static int reserved(void) {
    FILE *f=fopen("/proc/iomem","r"); if(!f) return 0;
    char line[256]; int found=0,safe=1;
    while(fgets(line,sizeof line,f)) {
        unsigned long long begin,end;
        if(strstr(line,"System RAM") && sscanf(line," %llx-%llx",&begin,&end)==2) {
            found=1;
            if(begin<(uint64_t)TM_PHYSICAL_BASE+4096 && end>=TM_PHYSICAL_BASE) safe=0;
        }
    }
    int failed=ferror(f); fclose(f); return found && safe && !failed;
}
static uint32_t reg(const unsigned char *memory,unsigned offset) {
    uint32_t value=*(volatile const uint32_t *)(memory+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST); return value;
}
typedef struct { uint32_t ticket,bytes,session,heartbeat; uint64_t sampled; } snapshot;
static int sample(const unsigned char *memory,snapshot *s) {
    if(reg(memory,TM_IDENTITY_OFFSET)!=TM_MAGIC ||
       reg(memory,TM_GEOMETRY_OFFSET)!=((TM_HEIGHT<<16)|TM_WIDTH)) return -1;
    s->session=reg(memory,TM_SESSION_ACK_OFFSET);
    s->ticket=reg(memory,TM_CART_META_OFFSET);
    s->bytes=reg(memory,TM_CART_META_OFFSET+4);
    s->heartbeat=reg(memory,TM_HEARTBEAT_OFFSET);
    s->sampled=now();
    if(s->ticket!=reg(memory,TM_CART_META_OFFSET) || !s->session ||
       s->session!=reg(memory,TM_SESSION_REQUEST_OFFSET) ||
       s->session!=reg(memory,TM_SESSION_ACK_OFFSET)) return 0;
    return reg(memory,TM_IDENTITY_OFFSET)==TM_MAGIC &&
        reg(memory,TM_GEOMETRY_OFFSET)==((TM_HEIGHT<<16)|TM_WIDTH) ? 1 : -1;
}
static int receiving(const snapshot *s,unsigned low,unsigned high) {
    return (s->ticket&3)==1 && s->bytes>=low && s->bytes<=high;
}
int main(int argc,char **argv) {
    unsigned pid=0,seconds=10,low=1024,high=65536; uint64_t expected_start=0;
    const char *exe=NULL,*hash=NULL,*core="/tmp/CORENAME",*memory_path="/dev/mem";
    int signo=0;
    for(int arg=1;arg<argc;++arg) {
        if(arg+1>=argc) return 2;
        const char *option=argv[arg++],*value=argv[arg];
        if(!strcmp(option,"--pid")) pid=(unsigned)number(value,INT_MAX);
        else if(!strcmp(option,"--starttime")) expected_start=number(value,UINT64_MAX);
        else if(!strcmp(option,"--exe")) exe=value;
        else if(!strcmp(option,"--sha256")) hash=value;
        else if(!strcmp(option,"--signal")) signo=!strcmp(value,"KILL")?SIGKILL:!strcmp(value,"TERM")?SIGTERM:0;
        else if(!strcmp(option,"--seconds")) seconds=(unsigned)number(value,60);
        else if(!strcmp(option,"--min-bytes")) low=(unsigned)number(value,TM_CART_CAPACITY-1);
        else if(!strcmp(option,"--max-bytes")) high=(unsigned)number(value,TM_CART_CAPACITY-1);
        else if(!strcmp(option,"--core-name")) core=value;
        else if(!strcmp(option,"--memory")) memory_path=value;
        else return 2;
    }
    int hardware=!strcmp(memory_path,"/dev/mem");
    if(pid<=1 || pid==(unsigned)getpid() || !expected_start || !exe || exe[0]!='/' ||
       !hash || strlen(hash)!=64 || strspn(hash,"0123456789abcdef")!=64 ||
       !signo || !seconds || !low || high<low || (hardware && strcmp(exe,"/media/fat/MiSTer"))) return 2;
    signal(SIGTERM,stop); signal(SIGINT,stop); setvbuf(stdout,NULL,_IONBF,0);
    int result=1,pidfd=-1,exe_fd=-1,memfd=-1;
    unsigned char *memory=MAP_FAILED; struct stat original,current;
    pidfd=(int)syscall(SYS_pidfd_open,pid,0);
    if(pidfd<0 || !selected(core) || !starttime(pid,expected_start)) goto cleanup;
    exe_fd=executable(pid,exe,&original);
    if(exe_fd<0 || !digest(exe_fd,hash) || !starttime(pid,expected_start) || !selected(core)) goto cleanup;
    if(hardware && !reserved()) goto cleanup;
    memfd=open(memory_path,O_RDONLY|O_SYNC|O_CLOEXEC); if(memfd<0) goto cleanup;
    struct stat memory_stat;
    if(fstat(memfd,&memory_stat) || (!hardware && (!S_ISREG(memory_stat.st_mode) || memory_stat.st_size<4096))) goto cleanup;
    memory=mmap(NULL,4096,PROT_READ,MAP_SHARED,memfd,hardware?TM_PHYSICAL_BASE:0);
    if(memory==MAP_FAILED) goto cleanup;
    uint64_t began=now(),heartbeat_at=began; uint32_t heartbeat=reg(memory,TM_HEARTBEAT_OFFSET);
    snapshot before;
    if(sample(memory,&before)!=1 || (before.ticket&3)==1) goto cleanup;
    printf("{\"event\":\"armed\",\"pid\":%u,\"starttime\":%llu,\"signal\":%d,\"initial_ticket\":%u,\"initial_session\":%u}\n",
           pid,(unsigned long long)expected_start,signo,before.ticket,before.session);
    while(!stopping && now()-began<(uint64_t)seconds*1000000000u) {
        if(!selected(core) || !starttime(pid,expected_start)) goto cleanup;
        int valid=sample(memory,&before); if(valid<0) goto cleanup;
        if(valid==1) {
            if(heartbeat!=before.heartbeat) { heartbeat=before.heartbeat; heartbeat_at=now(); }
            if(now()-heartbeat_at>1000000000u) goto cleanup;
            if(receiving(&before,low,high)) {
                int check=executable(pid,exe,&current);
                if(check<0) goto cleanup;
                close(check);
                if(!same_file(&original,&current) || !starttime(pid,expected_start) || !selected(core)) goto cleanup;
                snapshot trigger;
                if(sample(memory,&trigger)!=1 || trigger.ticket!=before.ticket ||
                   !receiving(&trigger,low,high)) continue;
                if(stopping) goto cleanup;
                uint64_t dispatch=now();
                // The coordinator journals the owned job before launch. Avoid
                // SD/pipe writes between the qualifying snapshot and this one
                // signal syscall. A missing reply never authorizes a retry.
                int sent=(int)syscall(SYS_pidfd_send_signal,pidfd,signo,NULL,0);
                uint64_t signal_return=now();
                printf("{\"event\":\"dispatch\",\"pid\":%u,\"signal\":%d,\"ticket\":%u,\"bytes\":%u,\"session\":%u,\"sampled_ns\":%llu,\"dispatch_ns\":%llu,\"signal_return_ns\":%llu,\"signal_result\":%d,\"single_dispatch\":true}\n",
                    pid,signo,trigger.ticket,trigger.bytes,trigger.session,
                    (unsigned long long)trigger.sampled,(unsigned long long)dispatch,
                    (unsigned long long)signal_return,sent);
                if(sent) goto cleanup;
                struct pollfd wait={pidfd,POLLIN,0}; int finished;
                do { finished=poll(&wait,1,5000); } while(finished<0 && errno==EINTR && !stopping);
                if(finished!=1 || !(wait.revents&POLLIN)) goto cleanup;
                snapshot after;
                if(!selected(core) || sample(memory,&after)!=1) goto cleanup;
                printf("{\"event\":\"departed\",\"pid\":%u,\"ticket\":%u,\"bytes\":%u,\"session\":%u,\"monotonic_ns\":%llu,\"still_receiving\":%s,\"within_window\":%s}\n",
                    pid,after.ticket,after.bytes,after.session,(unsigned long long)now(),
                    after.ticket==trigger.ticket && (after.ticket&3)==1 && after.bytes<TM_CART_CAPACITY?"true":"false",
                    after.ticket==trigger.ticket && receiving(&after,low,high)?"true":"false");
                // Passing requires the same transfer still incomplete after
                // target exit. It does not measure the exact signal boundary.
                result=after.ticket==trigger.ticket && receiving(&after,low,high)?0:1;
                goto cleanup;
            }
        }
        struct timespec pause={0,100000}; nanosleep(&pause,NULL);
    }
cleanup:
    if(result) fputs("Transfer signal not qualified; no signal retry performed\n",stderr);
    if(memory!=MAP_FAILED) munmap(memory,4096);
    if(memfd>=0) close(memfd);
    if(exe_fd>=0) close(exe_fd);
    if(pidfd>=0) close(pidfd);
    return result;
}
