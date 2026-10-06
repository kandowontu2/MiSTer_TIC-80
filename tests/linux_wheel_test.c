#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/linux_wheel.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real directory enumeration/open/close with synthetic kernel capabilities
 * and per-client event queues. No /dev/input devices are touched by this test. */
static char directory[128];
static struct {
    struct input_event queue[4096];
    unsigned count;
    int fine, coarse, disconnected;
} sources[4];
static struct { int source; unsigned cursor; } clients[1024];
static int opened, closed;
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64
#define __real_openat __real_openat64
#define __wrap_openat __wrap_openat64
#define __real_fstat __real_fstat64
#define __wrap_fstat __wrap_fstat64
#define __real_fstatat __real_fstatat64
#define __wrap_fstatat __wrap_fstatat64
#endif
int __real_openat(int,const char *,int,...);
int __real_close(int);
ssize_t __real_read(int,void *,size_t);
ssize_t __wrap_read(int,void *,size_t);
int __real_fstat(int,struct stat *);
int __real_fstatat(int,const char *,struct stat *,int);
static int number(const char *name) { return strlen(name)==6 && !strncmp(name,"event",5) && name[5]>='0' && name[5]<='3' ? name[5]-'0' : -1; }
int __wrap_openat(int dir,const char *name,int flags,...)
{
    int fd=__real_openat(dir,name,flags); int n=number(name);
    if(fd>=0 && n>=0) {
        assert(fd<1024 && (flags&O_NONBLOCK) && (flags&O_CLOEXEC) && (flags&O_NOFOLLOW));
        clients[fd].source=n+1;clients[fd].cursor=sources[n].count;++opened;
    }
    return fd;
}
int __wrap_close(int fd)
{
    if(fd>=0 && fd<1024 && clients[fd].source) { clients[fd].source=0;++closed; }
    return __real_close(fd);
}
int __wrap_fstat(int fd,struct stat *s)
{
    int result=__real_fstat(fd,s);
    if(!result && fd<1024 && clients[fd].source) { s->st_mode=S_IFCHR|0600;s->st_ctim=(struct timespec){0}; }
    return result;
}
int __wrap_fstatat(int fd,const char *name,struct stat *s,int flags)
{
    int result=__real_fstatat(fd,name,s,flags);
    if(!result && number(name)>=0) { s->st_mode=S_IFCHR|0600;s->st_ctim=(struct timespec){0}; }
    return result;
}
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64
int __real___fxstat64(int,int,struct stat *);
int __real___fxstatat64(int,int,const char *,struct stat *,int);
int __wrap___fxstat64(int version,int fd,struct stat *s)
{
    int result=__real___fxstat64(version,fd,s);
    if(!result && fd>=0 && fd<1024 && clients[fd].source) {s->st_mode=S_IFCHR|0600;s->st_ctim=(struct timespec){0};}
    return result;
}
int __wrap___fxstatat64(int version,int fd,const char *name,struct stat *s,int flags)
{
    int result=__real___fxstatat64(version,fd,name,s,flags);
    if(!result && number(name)>=0) {s->st_mode=S_IFCHR|0600;s->st_ctim=(struct timespec){0};}
    return result;
}
#else
int __real___fxstat(int,int,struct stat *);
int __real___fxstatat(int,int,const char *,struct stat *,int);
int __wrap___fxstat(int version,int fd,struct stat *s)
{
    int result=__real___fxstat(version,fd,s);
    if(!result && fd>=0 && fd<1024 && clients[fd].source) {s->st_mode=S_IFCHR|0600;s->st_ctim=(struct timespec){0};}
    return result;
}
int __wrap___fxstatat(int version,int fd,const char *name,struct stat *s,int flags)
{
    int result=__real___fxstatat(version,fd,name,s,flags);
    if(!result && number(name)>=0) {s->st_mode=S_IFCHR|0600;s->st_ctim=(struct timespec){0};}
    return result;
}
#endif
ssize_t __wrap___read_chk(int fd,void *data,size_t size,size_t capacity)
{
    assert(size<=capacity);
    return __wrap_read(fd,data,size);
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    assert(fd>=0 && fd<1024 && clients[fd].source);
    assert(_IOC_TYPE(request)=='E' && _IOC_NR(request)==0x20+EV_REL); /* no grab */
    va_list args;va_start(args,request);unsigned long *bits=va_arg(args,unsigned long *);va_end(args);
    unsigned n=clients[fd].source-1,w=8*sizeof(unsigned long);
    memset(bits,0,_IOC_SIZE(request));
    if(sources[n].coarse) bits[REL_HWHEEL/w]|=1UL<<(REL_HWHEEL%w);
    if(sources[n].fine) bits[REL_HWHEEL_HI_RES/w]|=1UL<<(REL_HWHEEL_HI_RES%w);
    return _IOC_SIZE(request);
}
ssize_t __wrap_read(int fd,void *out,size_t bytes)
{
    if(fd<0 || fd>=1024 || !clients[fd].source) return __real_read(fd,out,bytes);
    unsigned n=clients[fd].source-1;
    if(sources[n].disconnected) { errno=ENODEV;return -1; }
    unsigned count=sources[n].count-clients[fd].cursor;
    if(!count) {errno=EAGAIN;return -1;}
    if(count>bytes/sizeof(struct input_event))count=bytes/sizeof(struct input_event);
    memcpy(out,sources[n].queue+clients[fd].cursor,count*sizeof(struct input_event));clients[fd].cursor+=count;
    return count*sizeof(struct input_event);
}
static void node(unsigned n)
{
    char path[160];snprintf(path,sizeof path,"%s/event%u",directory,n);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);close(fd);
}
static void event(unsigned n,unsigned type,unsigned code,int value)
{
    assert(sources[n].count<4096);
    sources[n].queue[sources[n].count++]=(struct input_event){.type=type,.code=code,.value=value};
}
static void frame(unsigned n,unsigned code,int value)
{ event(n,EV_REL,code,value);event(n,EV_SYN,SYN_REPORT,0); }
int main(void)
{
    strcpy(directory,"/tmp/tic80-wheel-XXXXXX");assert(mkdtemp(directory));
    sources[0].coarse=1;sources[1].fine=sources[1].coarse=1;
    node(0);node(1);node(2); /* event2 lacks horizontal capabilities */
    tm_linux_wheel *s=tm_linux_wheel_open(directory);assert(s);
    uint64_t now=0;
    assert(tm_linux_wheel_poll(s,now++,1,0)==0);
    frame(0,REL_HWHEEL,3);assert(tm_linux_wheel_poll(s,now++,1,0)==3);
    frame(0,REL_HWHEEL,-5);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-2);
    frame(1,REL_HWHEEL_HI_RES,30);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-2);
    frame(1,REL_HWHEEL,1);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-2);
    frame(0,REL_HWHEEL,2);assert(tm_linux_wheel_poll(s,now++,1,0)==0);
    frame(1,REL_HWHEEL_HI_RES,90);assert(tm_linux_wheel_poll(s,now++,1,0)==1);
    frame(1,REL_HWHEEL_HI_RES,-250);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-1);
    frame(1,REL_HWHEEL_HI_RES,-110);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-2);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-2);
    event(1,EV_SYN,SYN_DROPPED,0);frame(1,REL_HWHEEL_HI_RES,999);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-2);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-1);
    frame(1,REL_HWHEEL_HI_RES,60);tm_linux_wheel_poll(s,now++,1,0);
    sources[1].disconnected=1;tm_linux_wheel_poll(s,now++,1,0);
    sources[1].disconnected=0;now=300;tm_linux_wheel_poll(s,now++,1,0);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,0)==(uint32_t)-1);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,0)==0);
    frame(1,REL_HWHEEL_HI_RES,60);tm_linux_wheel_poll(s,now++,1,0);
    frame(1,REL_HWHEEL_HI_RES,120);assert(tm_linux_wheel_poll(s,now++,0,1)==0);
    frame(0,REL_HWHEEL,100);assert(tm_linux_wheel_poll(s,now++,0,1)==0);
    assert(tm_linux_wheel_poll(s,now++,1,2)==0);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,2)==0);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,2)==1);
    frame(1,REL_HWHEEL_HI_RES,120);assert(tm_linux_wheel_poll(s,now++,1,4)==1); /* missed open/close */
    frame(0,REL_HWHEEL,65);assert(tm_linux_wheel_poll(s,now++,1,4)==66);
    frame(0,REL_HWHEEL,-63);assert(tm_linux_wheel_poll(s,now++,1,4)==3);
    /* Replacing an event node discards that device's old fractional motion. */
    frame(1,REL_HWHEEL_HI_RES,60);tm_linux_wheel_poll(s,now++,1,4);
    char old[160],path[160];snprintf(path,sizeof path,"%s/event1",directory);snprintf(old,sizeof old,"%s/old",directory);
    assert(!rename(path,old));node(1);now=600;tm_linux_wheel_poll(s,now++,1,4);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,4)==3);
    frame(1,REL_HWHEEL_HI_RES,60);assert(tm_linux_wheel_poll(s,now++,1,4)==4);
    /* More than one tick's queued input at a gate boundary must not leak. */
    for(unsigned n=0;n<700;++n)frame(0,REL_HWHEEL,1);
    assert(tm_linux_wheel_poll(s,now++,0,5)==4);
    assert(tm_linux_wheel_poll(s,now++,1,6)==4);
    now=900;assert(tm_linux_wheel_poll(s,now++,1,6)==4);
    frame(0,REL_HWHEEL,1);assert(tm_linux_wheel_poll(s,now++,1,6)==5);
    tm_linux_wheel_close(s);assert(opened==closed);
    s=tm_linux_wheel_open(directory);assert(tm_linux_wheel_poll(s,1000,1,0)==0);tm_linux_wheel_close(s);assert(opened==closed);
    for(unsigned n=0;n<3;++n){snprintf(path,sizeof path,"%s/event%u",directory,n);assert(!unlink(path));}
    assert(!unlink(old));assert(!rmdir(directory));
    puts("Linux wheel: independent devices, coarse/fine companions, loss, disconnect, node replacement, OSD edges/backlog and descriptor cleanup passed");
}
