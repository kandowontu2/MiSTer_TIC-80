#define _GNU_SOURCE
#include "tic80_mister/spawn_private.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"spawn failed line %d: %s (errno %d)\n",__LINE__,#c,errno); exit(1); } } while(0)
extern char **environ;
static atomic_bool stop;
static void handler(int signal) { (void)signal; }
static void *open_descriptors(void *argument)
{
    (void)argument;
    while(!atomic_load(&stop)) {
        int fd=open("/dev/null",O_RDONLY); CHECK(fd>=0);
        struct timespec delay={0,100000}; nanosleep(&delay,NULL); close(fd);
    }
    return NULL;
}
static int child(int argc,char **argv)
{
    CHECK(argc==7);
    int first=atoi(argv[2]),group=atoi(argv[3]),parent_group=atoi(argv[4]);
    bool stream=atoi(argv[5]); int count=atoi(argv[6]);
    CHECK(getpgrp()==(group?getpid():parent_group));
    sigset_t mask; CHECK(!sigprocmask(SIG_SETMASK,NULL,&mask));
    CHECK(sigismember(&mask,SIGUSR1)==1);
    struct sigaction action;
    CHECK(!sigaction(SIGUSR2,NULL,&action) && action.sa_handler==SIG_DFL);
    CHECK(!sigaction(SIGPIPE,NULL,&action) && action.sa_handler==SIG_IGN);
    for(int i=0;i<count;++i) {
        int fd=3+i; CHECK(!(fcntl(fd,F_GETFD)&FD_CLOEXEC));
        char expected=(char)('A'+i),actual=0; CHECK(pread(fd,&actual,1,0)==1 && actual==expected);
    }
    DIR *directory=opendir("/proc/self/fd"); CHECK(directory);
    int own=dirfd(directory); struct dirent *entry;
    while((entry=readdir(directory))) {
        if(entry->d_name[0]=='.') continue;
        int fd=atoi(entry->d_name); CHECK(fd<first || fd==own);
    }
    closedir(directory);
    if(stream) {
        char text; CHECK(read(0,&text,1)==0);
        char name[64]; ssize_t size=readlink("/proc/self/fd/2",name,sizeof name-1); CHECK(size>0);
        name[size]=0; CHECK(!strcmp(name,"/dev/null"));
        CHECK(write(1,"spawn-stream\n",13)==13);
    }
    return 0;
}
static void success(tm_spawn_request *request)
{
    pid_t pid=-123; CHECK(!tm_spawn_closed(&pid,request) && pid>0);
    int status; CHECK(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status));
}
static void failure(tm_spawn_request *request,int expected)
{
    pid_t pid=-123; int code=tm_spawn_closed(&pid,request);
    if(code!=expected || pid!=-123)
        fprintf(stderr,"spawn error file=%s search=%d expected=%d actual=%d pid=%ld\n",request->file,request->search_path,expected,code,(long)pid);
    CHECK(code==expected && pid==-123);
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--child")) return child(argc,argv);
    char executable[4096]; ssize_t size=readlink("/proc/self/exe",executable,sizeof executable-1); CHECK(size>0);
    executable[size]=0;
    sigset_t mask,original; sigemptyset(&mask); sigaddset(&mask,SIGUSR1);
    CHECK(!pthread_sigmask(SIG_BLOCK,&mask,&original));
    struct sigaction caught={.sa_handler=handler},ignored={.sa_handler=SIG_IGN};
    CHECK(!sigaction(SIGUSR2,&caught,NULL)); CHECK(!sigaction(SIGPIPE,&ignored,NULL));
    char parent_group[32]; snprintf(parent_group,sizeof parent_group,"%ld",(long)getpgrp());
    char *args[]={"spawn-test","--child","6","1",parent_group,"0","3",NULL};
    tm_spawn_fd fds[3];
    for(int i=0;i<3;++i) {
        int fd=memfd_create("spawn-payload",MFD_CLOEXEC); CHECK(fd>=0);
        char value=(char)('A'+i); CHECK(write(fd,&value,1)==1);
        fds[i]=(tm_spawn_fd){fcntl(fd,F_DUPFD_CLOEXEC,10),3+i}; CHECK(fds[i].source>=10); close(fd);
    }
    int junk=open("/dev/null",O_RDONLY); CHECK(junk>=0);
    struct rlimit limit; CHECK(!getrlimit(RLIMIT_NOFILE,&limit));
    int high=fcntl(junk,F_DUPFD,limit.rlim_cur>8192?8192:64); CHECK(high>=64);
    tm_spawn_request request={.file=executable,.argv=args,.envp=environ,.fds=fds,.count=3,.close_from=6,.process_group=true};
    request.file="/proc/self/exe"; success(&request); request.file=executable;
    pthread_t thread; CHECK(!pthread_create(&thread,NULL,open_descriptors,NULL));
    for(int i=0;i<24;++i) {
        request.count=(i%2)?2:3; request.close_from=request.count==2?5:6;
        args[2]=request.close_from==5?"5":"6"; args[6]=request.count==2?"2":"3";
        request.process_group=i%3!=0; args[3]=request.process_group?"1":"0";
        success(&request);
    }
    atomic_store(&stop,true); CHECK(!pthread_join(thread,NULL));
    request.count=0; request.fds=NULL; request.close_from=3; request.process_group=false;
    args[2]="3"; args[3]="0"; args[5]="1"; args[6]="0";
    int pair[2]; CHECK(!pipe2(pair,O_CLOEXEC));
    tm_spawn_fd output={fcntl(pair[1],F_DUPFD_CLOEXEC,10),1}; CHECK(output.source>=10); close(pair[1]);
    request.fds=&output; request.count=1; request.null_input=request.null_error=true;
    success(&request); close(output.source);
    char text[32]={0}; CHECK(read(pair[0],text,sizeof text)==13 && !strcmp(text,"spawn-stream\n"));
    CHECK(read(pair[0],text,sizeof text)==0); close(pair[0]);
    request.fds=NULL; request.count=0; request.null_input=request.null_error=false; args[5]="0";
    request.file="/TIC80-NONEXISTENT-SPAWN"; failure(&request,ENOENT);
    request.file=executable;
    int absent=fcntl(junk,F_DUPFD_CLOEXEC,64); CHECK(absent>=64); close(absent);
    tm_spawn_fd bad={absent,1}; request.fds=&bad; request.count=1; failure(&request,EBADF);
    bad=(tm_spawn_fd){fds[0].source,3}; failure(&request,EINVAL);
    request.fds=NULL; request.count=0;
    char folder[]="/tmp/tic80-spawn-XXXXXX"; CHECK(mkdtemp(folder));
    char first[4096],second[4096],target[4096],denied[4096],search[8192],plain[4096];
    snprintf(first,sizeof first,"%s/first",folder); snprintf(second,sizeof second,"%s/second",folder);
    CHECK(!mkdir(first,0700) && !mkdir(second,0700));
    snprintf(target,sizeof target,"%s/second/spawn-target",folder);
    snprintf(denied,sizeof denied,"%s/first/spawn-target",folder);
    int fd=open(denied,O_WRONLY|O_CREAT|O_EXCL,0600); CHECK(fd>=0); close(fd);
    CHECK(!symlink(executable,target)); snprintf(search,sizeof search,"%s:%s",first,second);
    const char *old_path=getenv("PATH"); char *saved=old_path?strdup(old_path):NULL;
    CHECK(!setenv("PATH",search,1)); request.file="spawn-target"; request.search_path=true;
    success(&request); CHECK(!unlink(target)); failure(&request,EACCES);
    CHECK(!unlink(denied)); failure(&request,ENOENT);
    CHECK(!symlink(executable,target)); char cwd[4096]; CHECK(getcwd(cwd,sizeof cwd));
    CHECK(!chdir(second) && !setenv("PATH","",1)); success(&request); CHECK(!chdir(cwd));
    snprintf(plain,sizeof plain,"%s/second/plain",folder);
    fd=open(plain,O_WRONLY|O_CREAT|O_EXCL,0700); CHECK(fd>=0); CHECK(write(fd,"exit 0\n",7)==7); close(fd);
    request.file=plain; failure(&request,ENOEXEC);
    char not_directory[4096]; snprintf(not_directory,sizeof not_directory,"%s/second/plain/child",folder);
    request.file=not_directory; request.search_path=false; failure(&request,ENOTDIR);
    request.search_path=true; failure(&request,ENOTDIR);
    if(saved) { CHECK(!setenv("PATH",saved,1)); free(saved); } else CHECK(!unsetenv("PATH"));
    CHECK(!unlink(plain) && !unlink(target) && !rmdir(first) && !rmdir(second) && !rmdir(folder));
    CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&mask) && sigismember(&mask,SIGUSR1)==1);
    CHECK(!sigaction(SIGUSR2,NULL,&caught) && caught.sa_handler==handler);
    CHECK(!pthread_sigmask(SIG_SETMASK,&original,NULL));
    for(int i=0;i<3;++i) close(fds[i].source);
    close(high); close(junk);
    puts("Spawn: concurrent descriptor isolation, 3/4/5 retention, process groups, null streams, signals, PATH/error cleanup and no-shell execution passed");
    return 0;
}
