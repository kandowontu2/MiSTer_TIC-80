#define _GNU_SOURCE
#include "studio/net.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include "tic80_mister/spawn_private.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* MiSTer's bundled curl supplies HTTPS without linking the host's TLS ABI.
 * There is no shell. Children inherit only stdin/stdout/stderr, and completions
 * are dispatched by tic_net_end on the Studio thread. */
#define URL_LIMIT 2048
#define BODY_LIMIT (8u*1024u*1024u)
#define EXPORT_LIMIT (64u*1024u*1024u)
#define QUEUE_LIMIT 16
#define ACTIVE_LIMIT 4
#define PUMP_BYTES (64u*1024u)
#define TIMEOUT_NS (20ULL*1000000000ULL)
extern char **environ;
typedef struct Request {
    struct Request *next;
    char url[URL_LIMIT];
    net_get_callback callback;
    void *calldata;
    unsigned char *body;
    size_t size, capacity, limit;
    uint64_t id, started;
    pid_t pid;
    int fd, status, error;
    bool eof, exited;
} Request;
struct tic_net {
    char host[URL_LIMIT];
    char ca[PATH_MAX];
    Request *head, *tail;
    unsigned count, running;
    uint64_t serial;
    bool pumping, closing;
};
static uint64_t now(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000ULL+ts.tv_nsec;
}
static bool http(const char *url)
{ return url && (!strncmp(url,"http://",7)||!strncmp(url,"https://",8)); }
static bool hex(unsigned char ch)
{ return (ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')||(ch>='A'&&ch<='F'); }
static bool address(char *out,const char *host,const char *path)
{
    size_t at=strlen(host); memcpy(out,host,at);
    const char digits[]="0123456789ABCDEF";
    for(const unsigned char *p=(const unsigned char *)path;*p;++p) {
        bool escape=*p<33||*p>=127||*p=='#'||(*p=='%'&&!(p[1]&&p[2]&&hex(p[1])&&hex(p[2])));
        size_t width=escape?3:1;
        if(at+width>=URL_LIMIT) return false;
        if(escape) { out[at++]='%'; out[at++]=digits[*p>>4]; out[at++]=digits[*p&15]; }
        else out[at++]=*p;
    }
    out[at]=0; return true;
}
static void error_callback(const char *url,net_get_callback callback,void *data,int code)
{
    if(callback) {
        net_get_data result={.type=net_get_error,.calldata=data,.url=url,.error.code=code};
        callback(&result);
    }
}
tic_net *tic_net_create(const char *host)
{
    if(!http(host)||strlen(host)>=URL_LIMIT) return NULL;
    tic_net *net=calloc(1,sizeof *net);
    if(net) {
        strcpy(net->host,host);
        const char *configured=getenv("TIC80_CA_BUNDLE");
        if(configured&&*configured) {
            if(strlen(configured)>=sizeof net->ca||access(configured,R_OK)) { free(net); return NULL; }
            strcpy(net->ca,configured);
        } else {
            char executable[PATH_MAX];
            ssize_t length=readlink("/proc/self/exe",executable,sizeof executable-1);
            if(length>0) {
                executable[length]=0; char *slash=strrchr(executable,'/');
                if(slash) {
                    slash[1]=0;
                    int size=snprintf(net->ca,sizeof net->ca,"%scacert.pem",executable);
                    if(size<0||size>=(int)sizeof net->ca||access(net->ca,R_OK)) net->ca[0]=0;
                }
            }
        }
    }
    return net;
}
void tic_net_get(tic_net *net,const char *path,net_get_callback callback,void *data)
{
    if(!callback) return;
    if(!net||net->closing||net->count>=QUEUE_LIMIT) {
        error_callback(path,callback,data,-EAGAIN); return;
    }
    Request *req=calloc(1,sizeof *req);
    if(!req) { error_callback(path,callback,data,-ENOMEM); return; }
    req->fd=-1; req->callback=callback; req->calldata=data; req->id=++net->serial;
    req->limit=path&&!strncmp(path,"/export/",8)?EXPORT_LIMIT:BODY_LIMIT;
    if(!path||*path!='/') req->error=-EINVAL;
    else if(!address(req->url,net->host,path))
        req->error=-ENAMETOOLONG;
    if(net->tail) net->tail->next=req; else net->head=req;
    net->tail=req; ++net->count;
}
static int start(Request *req,const char *ca)
{
    int pair[2];
    if(pipe2(pair,O_CLOEXEC)) return -errno;
    int source=fcntl(pair[1],F_DUPFD_CLOEXEC,10);
    close(pair[1]);
    if(source<0) { int code=-errno; close(pair[0]); return code; }
    int flags=fcntl(pair[0],F_GETFL);
    if(flags<0||fcntl(pair[0],F_SETFL,flags|O_NONBLOCK)<0) {
        int code=-errno; close(pair[0]); close(source); return code;
    }
    char limit[32]; snprintf(limit,sizeof limit,"%zu",req->limit);
    char *argv[]={"curl","-q","--globoff","--fail","--location","--silent",
        "--connect-timeout","5","--max-time","15","--max-redirs","5",
        "--max-filesize",limit,"--proto","=http,https","--proto-redir","=http,https",
        "--header","accept: */*","--write-out","\n%{http_code}","--url",req->url,NULL,NULL,NULL};
    if(*ca) {
        size_t count=0; while(argv[count]) ++count;
        argv[count++]="--cacert"; argv[count++]=(char *)ca; argv[count]=NULL;
    }
    tm_spawn_fd output={source,1};
    tm_spawn_request spawn={.file="curl",.argv=argv,.envp=environ,
        .fds=&output,.count=1,.close_from=3,.search_path=true,.null_input=true,.null_error=true};
    int code=tm_spawn_closed(&req->pid,&spawn);
    close(source);
    if(code) { close(pair[0]); req->pid=0; return -code; }
    req->fd=pair[0]; req->started=now();
    return 0;
}
static void terminate(Request *req)
{
    if(req->pid&&!req->exited) kill(req->pid,SIGKILL);
    if(req->fd>=0) { close(req->fd); req->fd=-1; }
    req->eof=true;
}
static void dispose(Request *req)
{
    terminate(req);
    if(req->pid&&!req->exited)
        while(waitpid(req->pid,NULL,0)<0&&errno==EINTR) {}
    free(req->body); free(req);
}
void tic_net_close(tic_net *net)
{
    if(!net||net->closing) return;
    net->closing=true;
    Request *req=net->head;
    net->head=net->tail=NULL; net->count=net->running=0;
    while(req) {
        Request *next=req->next;
        terminate(req);
        error_callback(req->url,req->callback,req->calldata,-ECANCELED);
        dispose(req); req=next;
    }
    if(!net->pumping) free(net);
}
void tic_net_start(tic_net *net) { (void)net; }
static bool append(Request *req,const unsigned char *bytes,size_t count)
{
    if(count>req->limit+4-req->size) { req->error=-EFBIG; return false; }
    size_t need=req->size+count+1;
    if(need>req->capacity) {
        size_t capacity=req->capacity?req->capacity*2:8192;
        if(capacity<need) capacity=need;
        if(capacity>req->limit+5) capacity=req->limit+5;
        unsigned char *next=realloc(req->body,capacity);
        if(!next) { req->error=-ENOMEM; return false; }
        req->body=next; req->capacity=capacity;
    }
    memcpy(req->body+req->size,bytes,count); req->size+=count; req->body[req->size]=0;
    return true;
}
static void pump(Request *req,size_t *budget)
{
    if(req->pid&&!req->exited&&now()-req->started>TIMEOUT_NS) {
        req->error=-ETIMEDOUT; terminate(req);
    }
    unsigned char bytes[8192];
    while(req->fd>=0&&*budget) {
        size_t wanted=*budget<sizeof bytes?*budget:sizeof bytes;
        ssize_t size=read(req->fd,bytes,wanted);
        if(size>0) {
            *budget-=(size_t)size;
            if(!append(req,bytes,(size_t)size)) { terminate(req); break; }
        } else if(size==0) { close(req->fd); req->fd=-1; req->eof=true; }
        else if(errno==EINTR) continue;
        else if(errno==EAGAIN) break;
        else { req->error=-errno; terminate(req); }
    }
    if(req->pid&&!req->exited) {
        pid_t result=waitpid(req->pid,&req->status,WNOHANG);
        if(result==req->pid) req->exited=true;
        else if(result<0&&errno!=EINTR) { req->error=-errno; req->exited=true; }
    }
}
static void complete(Request *req)
{
    int status=0;
    if(req->size>=4) {
        const unsigned char *tail=req->body+req->size-4;
        if(tail[0]=='\n'&&tail[1]>='0'&&tail[1]<='9'&&tail[2]>='0'&&tail[2]<='9'&&tail[3]>='0'&&tail[3]<='9') {
            status=(tail[1]-'0')*100+(tail[2]-'0')*10+tail[3]-'0';
            req->size-=4; req->body[req->size]=0;
        }
    }
    net_get_data result={.calldata=req->calldata,.url=req->url};
    if(!req->error&&req->exited&&WIFEXITED(req->status)&&WEXITSTATUS(req->status)==0&&status==200) {
        result.type=net_get_done; result.done.size=(s32)req->size; result.done.data=req->body;
    } else {
        result.type=net_get_error;
        int exit_code=WIFEXITED(req->status)?WEXITSTATUS(req->status):0;
        result.error.code=req->error?req->error:exit_code==63?-EFBIG:exit_code==28?-ETIMEDOUT:
            status>=100&&status!=200?status:exit_code?-exit_code:-EIO;
    }
    req->callback(&result);
}
void tic_net_end(tic_net *net)
{
    if(!net||net->pumping) return;
    net->pumping=true;
    /* New requests queued by callbacks belong to the next frame. */
    const uint64_t last=net->serial;
    size_t budget=PUMP_BYTES;
    Request **link=&net->head;
    while(*link&&!net->closing) {
        Request *req=*link;
        if(req->id>last) break;
        if(!req->pid&&!req->error&&net->running<ACTIVE_LIMIT) {
            req->error=start(req,net->ca);
            if(req->pid) ++net->running;
        }
        if(req->pid) pump(req,&budget);
        if((req->error&&!req->pid)||(req->exited&&req->eof)) {
            *link=req->next;
            if(net->tail==req) {
                net->tail=net->head;
                if(net->tail) while(net->tail->next) net->tail=net->tail->next;
            }
            --net->count; if(req->pid) --net->running;
            complete(req); dispose(req);
            if(net->closing) break;
        } else link=&req->next;
    }
    net->pumping=false;
    if(net->closing) free(net);
}
