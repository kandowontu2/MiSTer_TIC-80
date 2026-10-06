#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/hid_wheel.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The supervisor, real spawn/exec, Unix socket, directory enumeration, normal
 * file opens/closes and child lifecycle are production code. Only character
 * metadata and the HID kernel ioctls/report queue are substituted. No real
 * device nodes are opened. Fixture nodes use length-prefixed append-only wire
 * packets to model one HID read per report and EAGAIN at the current tail. */
static char fixture[256];
static int device_fd[4096];
static char device_name[4096][32];
static pid_t last_child;
static int worker_process;
static int pinned_nodes[128];
static unsigned pinned_count;
extern int __real_openat(int,const char *,int,...);
extern int __real_openat64(int,const char *,int,...);
extern int __real_close(int);
extern ssize_t __real_read(int,void *,size_t);
extern int __real_fstat(int,struct stat *);
extern int __real_fstat64(int,struct stat64 *);
extern int __real_fstatat(int,const char *,struct stat *,int);
extern int __real_fstatat64(int,const char *,struct stat64 *,int);
extern int __real___fxstat(int,int,struct stat *);
extern int __real___fxstat64(int,int,struct stat64 *);
extern int __real___fxstatat(int,int,const char *,struct stat *,int);
extern int __real___fxstatat64(int,int,const char *,struct stat64 *,int);
extern int __real_posix_spawn(pid_t *,const char *,const posix_spawn_file_actions_t *,
                            const posix_spawnattr_t *,char *const[],char *const[]);
extern int __real_socketpair(int,int,int,int[2]);
int __wrap_socketpair(int domain,int type,int protocol,int sockets[2])
{
    char path[512];snprintf(path,sizeof path,"%s/probe-socket-fail",fixture);
    if(worker_process && !access(path,F_OK)) {errno=EMFILE;return -1;}
    return __real_socketpair(domain,type,protocol,sockets);
}
static int numeric_node(const char *name)
{
    if(strncmp(name,"hidraw",6) || !name[6]) return 0;
    for(name+=6;*name;++name) if(*name<'0' || *name>'9') return 0;
    return 1;
}
static int fixture_directory(int fd)
{
    char proc[64],target[512]; snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(proc,target,sizeof target-1);
    if(n<0) return 0;
    target[n]=0;
    return !strcmp(target,fixture);
}
static void remember(int fd,const char *name,int flags)
{
    if(fd<0 || !numeric_node(name)) return;
    assert(fd<4096 && strlen(name)<sizeof device_name[0]);
    assert((flags&(O_ACCMODE|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW)) ==
           (O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));
    device_fd[fd]=1; strcpy(device_name[fd],name);
    /* Opening a hidraw client begins with an empty per-client kernel queue. */
    assert(lseek(fd,0,SEEK_END)>=0);
}
static void block_open(int dir,const char *name)
{
    if(!fixture_directory(dir) || !numeric_node(name)) return;
    char path[512];snprintf(path,sizeof path,"%s/%s.block-open",fixture,name);
    if(!access(path,F_OK)) {for(;;) pause();}
}
static int kernel_lock(int exclusive)
{
    char path[512];snprintf(path,sizeof path,"%s/.kernel-lock",fixture);
    int fd=open(path,O_RDONLY|O_CLOEXEC);
    if(fd<0) {assert(errno==ENOENT);return -1;}
    assert(!flock(fd,exclusive?LOCK_EX:LOCK_SH));return fd;
}
static void kernel_unlock(int fd)
{
    if(fd>=0) {assert(!flock(fd,LOCK_UN));assert(!__real_close(fd));}
}
int __wrap_openat(int dir,const char *name,int flags,...)
{
    block_open(dir,name);
    int lock=fixture_directory(dir) && numeric_node(name)?kernel_lock(1):-1;
    mode_t mode=0; if(flags&O_CREAT) { va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap); }
    int fd=__real_openat(dir,name,flags,mode);
    if(fixture_directory(dir)) remember(fd,name,flags);
    kernel_unlock(lock);
    return fd;
}
int __wrap_openat64(int dir,const char *name,int flags,...)
{
    block_open(dir,name);
    int lock=fixture_directory(dir) && numeric_node(name)?kernel_lock(1):-1;
    mode_t mode=0; if(flags&O_CREAT) { va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap); }
    int fd=__real_openat64(dir,name,flags,mode);
    if(fixture_directory(dir)) remember(fd,name,flags);
    kernel_unlock(lock);
    return fd;
}
int __wrap_close(int fd)
{
    int lock=fd>=0 && fd<4096 && device_fd[fd]?kernel_lock(1):-1;
    if(fd>=0 && fd<4096 && device_fd[fd]) {
        char path[512];snprintf(path,sizeof path,"%s/%s.block-close",fixture,device_name[fd]);
        if(!access(path,F_OK)) {for(;;) pause();}
    }
    if(fd>=0 && fd<4096) {device_fd[fd]=0;device_name[fd][0]=0;}
    int result=__real_close(fd);kernel_unlock(lock);return result;
}
static void character(struct stat *s)
{
    s->st_mode=(s->st_mode&0777)|S_IFCHR;s->st_rdev=42;s->st_ctim=(struct timespec){0};
}
static void character64(struct stat64 *s)
{
    s->st_mode=(s->st_mode&0777)|S_IFCHR;s->st_rdev=42;s->st_ctim=(struct timespec){0};
}
int __wrap_fstat(int fd,struct stat *s)
{
    int r=__real_fstat(fd,s);if(!r && fd<4096 && device_fd[fd]) character(s);return r;
}
int __wrap_fstat64(int fd,struct stat64 *s)
{
    int r=__real_fstat64(fd,s);if(!r && fd<4096 && device_fd[fd]) character64(s);return r;
}
int __wrap_fstatat(int fd,const char *name,struct stat *s,int flags)
{
    int r=__real_fstatat(fd,name,s,flags);
    if(!r && numeric_node(name) && fixture_directory(fd) && S_ISREG(s->st_mode)) character(s);
    return r;
}
int __wrap_fstatat64(int fd,const char *name,struct stat64 *s,int flags)
{
    int r=__real_fstatat64(fd,name,s,flags);
    if(!r && numeric_node(name) && fixture_directory(fd) && S_ISREG(s->st_mode)) character64(s);
    return r;
}
/* SDK glibc headers inline Release-mode stat calls through their versioned
 * helpers. Cover those entry points as well as the public stat functions. */
int __wrap___fxstat(int version,int fd,struct stat *s)
{
    int r=__real___fxstat(version,fd,s);
    if(!r && fd>=0 && fd<4096 && device_fd[fd]) character(s);
    return r;
}
int __wrap___fxstat64(int version,int fd,struct stat64 *s)
{
    int r=__real___fxstat64(version,fd,s);
    if(!r && fd>=0 && fd<4096 && device_fd[fd]) character64(s);
    return r;
}
int __wrap___fxstatat(int version,int fd,const char *name,struct stat *s,int flags)
{
    int r=__real___fxstatat(version,fd,name,s,flags);
    if(!r && numeric_node(name) && fixture_directory(fd) && S_ISREG(s->st_mode)) character(s);
    return r;
}
int __wrap___fxstatat64(int version,int fd,const char *name,struct stat64 *s,int flags)
{
    int r=__real___fxstatat64(version,fd,name,s,flags);
    if(!r && numeric_node(name) && fixture_directory(fd) && S_ISREG(s->st_mode)) character64(s);
    return r;
}
ssize_t __wrap_read(int fd,void *data,size_t size)
{
    if(fd<0 || fd>=4096 || !device_fd[fd]) return __real_read(fd,data,size);
    struct stat s;assert(!__real_fstat(fd,&s));
    if(!s.st_nlink) {errno=ENODEV;return -1;}
    off_t offset=lseek(fd,0,SEEK_CUR);assert(offset>=0);
    uint8_t header[2];ssize_t n=pread(fd,header,2,offset);
    if(n!=2) {errno=EAGAIN;return -1;}
    unsigned length=header[0]|((unsigned)header[1]<<8);
    assert(length && length<=size);
    n=pread(fd,data,length,offset+2);
    if(n!=(ssize_t)length) {errno=EAGAIN;return -1;}
    assert(lseek(fd,offset+2+length,SEEK_SET)>=0);
    return n;
}
ssize_t __wrap___read_chk(int fd,void *data,size_t size,size_t capacity)
{
    assert(size<=capacity);
    return __wrap_read(fd,data,size);
}
static size_t descriptor_read(int fd,uint8_t *data,size_t size)
{
    char path[512];snprintf(path,sizeof path,"%s/%s.descriptor",fixture,device_name[fd]);
    int f=open(path,O_RDONLY);assert(f>=0);
    ssize_t n=__real_read(f,data,size);assert(n>=0);close(f);return (size_t)n;
}
static int device_ioctl(int fd,unsigned long request,void *data)
{
    assert(worker_process);
    assert(fd>=0 && fd<4096 && device_fd[fd]);
    char logpath[512];snprintf(logpath,sizeof logpath,"%s/%s.queries",fixture,device_name[fd]);
    int log=open(logpath,O_WRONLY|O_CREAT|O_APPEND,0600);assert(log>=0);
    uint8_t query=(uint8_t)_IOC_NR(request);assert(write(log,&query,1)==1);close(log);
    char path[512];snprintf(path,sizeof path,"%s/%s.block",fixture,device_name[fd]);
    if(!access(path,F_OK)) {for(;;) pause();} /* Uninterruptible-to-normal-flow probe model. */
    uint8_t desc[4096];size_t length=descriptor_read(fd,desc,sizeof desc);
    if(request==HIDIOCGRDESCSIZE) {
        snprintf(path,sizeof path,"%s/%s.descriptor-fail",fixture,device_name[fd]);
        if(!access(path,F_OK)) {errno=EIO;return -1;}
        *(int *)data=(int)length;return 0;
    }
    if(request==HIDIOCGRDESC) {
        struct hidraw_report_descriptor *d=data;assert(d->size==length);
        memcpy(d->value,desc,length);return 0;
    }
    /* Only GET is allowed. Any attempt to set device features or grab evdev
     * fails this test immediately, in the actual re-executed worker. */
    assert(_IOC_TYPE(request)=='H' && _IOC_NR(request)==0x07);
    snprintf(path,sizeof path,"%s/%s.block-feature",fixture,device_name[fd]);
    if(!access(path,F_OK)) {for(;;) pause();}
    snprintf(path,sizeof path,"%s/%s.feature-fail",fixture,device_name[fd]);
    if(!access(path,F_OK)) {errno=EIO;return -1;}
    uint8_t *feature=data;unsigned bytes=_IOC_SIZE(request),id=feature[0];
    snprintf(path,sizeof path,"%s/%s.feature-%u",fixture,device_name[fd],id);
    int f=open(path,O_RDONLY);if(f<0) return -1;
    ssize_t n=__real_read(f,feature,bytes);close(f);return (int)n;
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    va_list ap;va_start(ap,request);void *data=va_arg(ap,void *);va_end(ap);
    int lock=kernel_lock(0), result=device_ioctl(fd,request,data);
    kernel_unlock(lock);return result;
}
int __wrap_posix_spawn(pid_t *pid,const char *file,const posix_spawn_file_actions_t *actions,
                       const posix_spawnattr_t *attrs,char *const args[],char *const env[])
{
    int r=__real_posix_spawn(pid,file,actions,attrs,args,env);
    if(!r) last_child=*pid;
    return r;
}
static uint64_t now(void)
{
    struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void write_file(const char *name,const void *data,size_t size)
{
    char path[512];snprintf(path,sizeof path,"%s/%s",fixture,name);
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
    if(size) assert(write(fd,data,size)==(ssize_t)size);
    assert(!close(fd));
}
static void remove_file(const char *name)
{
    char path[512];snprintf(path,sizeof path,"%s/%s",fixture,name);assert(!unlink(path));
}
static void packet(const char *name,const uint8_t *data,unsigned size)
{
    uint8_t buffer[512];assert(size && size+2<=sizeof buffer);
    buffer[0]=(uint8_t)size;buffer[1]=(uint8_t)(size>>8);memcpy(buffer+2,data,size);
    char path[512];snprintf(path,sizeof path,"%s/%s",fixture,name);
    int fd=open(path,O_WRONLY|O_APPEND);assert(fd>=0);
    assert(write(fd,buffer,size+2)==(ssize_t)(size+2));assert(!close(fd));
}
static uint32_t pump(tm_hid_wheel *s,int active,unsigned epoch,unsigned duration)
{
    uint64_t until=now()+duration;uint32_t total;
    do {
        uint64_t start=now();total=tm_hid_wheel_poll(s,start,active,epoch);
        /* No device ioctl or wait-for-child is allowed in this call. The
         * bound tolerates loaded CI hosts without treating it as a frame SLA. */
        assert(now()-start<500);usleep(5000);
    } while(now()<until);
    return total;
}
static void expect(tm_hid_wheel *s,int active,unsigned epoch,uint32_t expected)
{
    uint64_t limit=now()+1500;uint32_t total;
    do {total=tm_hid_wheel_poll(s,now(),active,epoch);if(total==expected) break;usleep(5000);}while(now()<limit);
    if(total!=expected) fprintf(stderr,"wheel total %u != %u\n",total,expected);
    assert(total==expected);
    assert(pump(s,active,epoch,40)==expected);
}
static const uint8_t coarse[] = {
    0x05,0x0c,0x0a,0x38,2,0x15,0x81,0x25,0x7f,0x75,8,0x95,1,0x81,6
};
/* Two numbered reports in one logical collection: GET report 2 selects scale
 * eight; input report 1 carries signed pan. */
static const uint8_t fine[] = {
    0xa1,1,0xa1,2,0x85,2,0x05,1,0x09,0x48,0x15,0,0x25,1,
    0x35,1,0x45,8,0x75,8,0x95,1,0xb1,2,
    0x85,1,0x05,0x0c,0x0a,0x38,2,0x15,0x81,0x25,0x7f,
    0x75,8,0x95,1,0x81,6,0xc0,0xc0
};
static void device_create(const char *name,const uint8_t *desc,size_t size)
{
    char field[64];snprintf(field,sizeof field,"%s.descriptor",name);write_file(field,desc,size);
    write_file(name,NULL,0);
    /* The append-only regular-file transport fixes ctime at zero, because
     * appending a report must not look like devtmpfs node replacement. Pin
     * fixture inodes so an unlink/recreate cannot reuse that fake identity. */
    char path[512];snprintf(path,sizeof path,"%s/%s",fixture,name);
    assert(pinned_count<sizeof pinned_nodes/sizeof pinned_nodes[0]);
    int fd=open(path,O_RDONLY|O_CLOEXEC);assert(fd>=0);pinned_nodes[pinned_count++]=fd;
}
static void probes_bounded(pid_t worker,unsigned maximum)
{
    char path[128],bytes[4096];
    snprintf(path,sizeof path,"/proc/%ld/task/%ld/children",(long)worker,(long)worker);
    int fd=open(path,O_RDONLY);assert(fd>=0);
    ssize_t count=__real_read(fd,bytes,sizeof bytes-1);assert(count>=0);close(fd);bytes[count]=0;
    unsigned children=0;char *p=bytes;
    for(;;) {
        char *end;long pid=strtol(p,&end,10);
        if(end==p) break;
        assert(pid>0);++children;p=end;
    }
    assert(children<=maximum);
}
static unsigned query_count(const char *name)
{
    char path[512];snprintf(path,sizeof path,"%s/%s.queries",fixture,name);
    struct stat metadata;assert(!stat(path,&metadata));return (unsigned)metadata.st_size;
}
static void lifecycle(void)
{
    device_create("hidraw0",coarse,sizeof coarse);
    device_create("hidraw1",fine,sizeof fine);uint8_t feature[]={2,1};
    write_file("hidraw1.feature-2",feature,sizeof feature);
    write_file("hidrawX",NULL,0); /* Non-numeric nodes and symlinks are ignored. */
    char link[512],target[512];snprintf(link,sizeof link,"%s/hidraw99",fixture);
    snprintf(target,sizeof target,"%s/hidraw0",fixture);assert(!symlink(target,link));
    tm_hid_wheel *s=tm_hid_wheel_open(fixture);assert(s);
    assert(!pump(s,1,0,350));assert(last_child>0);
    uint8_t one=1,minus=0xff;packet("hidraw0",&one,1);expect(s,1,0,1);
    packet("hidraw0",&minus,1);expect(s,1,0,0); /* Negative wrapping total. */
    uint8_t tick[]={1,1};for(unsigned n=0;n<7;++n) packet("hidraw1",tick,2);
    assert(!pump(s,1,0,60));packet("hidraw1",tick,2);expect(s,1,0,1);
    /* A complete OSD cycle between frontend polls changes the FPGA epoch.
     * Stale responses already queued in the socket must not count. */
    for(unsigned n=0;n<20;++n) packet("hidraw0",&one,1);
    usleep(30000);
    assert(pump(s,1,2,100)==1);
    packet("hidraw0",&one,1);expect(s,1,2,2);
    assert(pump(s,0,3,50)==2);
    for(unsigned n=0;n<20;++n) packet("hidraw0",&one,1);
    assert(pump(s,0,3,70)==2 && pump(s,1,4,70)==2);
    packet("hidraw0",&one,1);expect(s,1,4,3);
    /* Fractional motion and queued detents cannot survive a gate edge. */
    for(unsigned n=0;n<7;++n) packet("hidraw1",tick,2);
    assert(pump(s,1,4,40)==3 && pump(s,1,6,40)==3);
    packet("hidraw1",tick,2);assert(pump(s,1,6,40)==3);
    for(unsigned n=0;n<7;++n) packet("hidraw1",tick,2);
    expect(s,1,6,4);
    /* Truncated known reports discard fractions without producing a tick. */
    for(unsigned n=0;n<7;++n) packet("hidraw1",tick,2);
    uint8_t short_report=1;packet("hidraw1",&short_report,1);packet("hidraw1",tick,2);
    assert(pump(s,1,6,50)==4);
    /* Node replacement discards the old raw queue and resolves a fresh node. */
    remove_file("hidraw1");device_create("hidraw1",fine,sizeof fine);
    assert(pump(s,1,6,350)==4);
    for(unsigned n=0;n<7;++n) packet("hidraw1",tick,2);
    assert(pump(s,1,6,40)==4);packet("hidraw1",tick,2);expect(s,1,6,5);
    /* Parent backpressure coalesces cumulative totals without losing a burst. */
    for(unsigned n=0;n<600;++n) packet("hidraw0",&one,1);
    usleep(60000);expect(s,1,6,605);
    /* Unexpected worker death is reaped and restarted with a fresh baseline. */
    pid_t previous=last_child;assert(!kill(previous,SIGKILL));
    assert(pump(s,1,6,1300)==605 && last_child!=previous);
    packet("hidraw0",&one,1);expect(s,1,6,606);
    /* Persistently stuck probes must not stop existing devices, new discovery,
     * or the reader heartbeat. Leave bad nodes present beyond both the probe
     * deadline and frontend watchdog, then replace one node to reprobe it. */
    device_create("hidraw2",coarse,sizeof coarse);write_file("hidraw2.block",NULL,0);
    previous=last_child;assert(pump(s,1,6,350)==606);
    packet("hidraw0",&one,1);expect(s,1,6,607);
    device_create("hidraw5",fine,sizeof fine);write_file("hidraw5.feature-2",feature,sizeof feature);
    write_file("hidraw5.block-feature",NULL,0);
    device_create("hidraw6",coarse,sizeof coarse);
    assert(pump(s,1,6,350)==607 && last_child==previous);
    packet("hidraw6",&one,1);expect(s,1,6,608);
    assert(pump(s,1,6,3500)==608 && last_child==previous);
    /* At most one helper per each of the five present numeric device nodes,
     * including streaming healthy nodes and stalled configuration nodes. */
    probes_bounded(previous,5);
    packet("hidraw0",&one,1);packet("hidraw6",&one,1);expect(s,1,6,610);
    remove_file("hidraw2");remove_file("hidraw2.block");device_create("hidraw2",coarse,sizeof coarse);
    assert(pump(s,1,6,350)==610 && last_child==previous);
    packet("hidraw2",&one,1);expect(s,1,6,611);
    /* USB supplies a zero ID prefix for unnumbered GET reports; HIDP/UHID
     * may supply only the payload. Both must decode the same physical scale. */
    uint8_t unnumbered[sizeof fine];size_t length=0;
    for(size_t n=0;n<sizeof fine;++n) {
        if(fine[n]==0x85) {++n;continue;}
        unnumbered[length++]=fine[n];
    }
    device_create("hidraw3",unnumbered,length);device_create("hidraw4",unnumbered,length);
    uint8_t usb_feature[]={0,1},bluetooth_feature=1;
    write_file("hidraw3.feature-0",usb_feature,2);
    write_file("hidraw4.feature-0",&bluetooth_feature,1);
    assert(pump(s,1,6,350)==611);
    for(unsigned n=0;n<8;++n) {packet("hidraw3",&one,1);packet("hidraw4",&one,1);}
    expect(s,1,6,613);
    /* Feature/descriptor transport failures recover on the same device node.
     * Motion queued before recovery must not burst into gameplay afterward. */
    device_create("hidraw7",fine,sizeof fine);write_file("hidraw7.feature-2",feature,sizeof feature);
    write_file("hidraw7.feature-fail",NULL,0);
    assert(pump(s,1,6,350)==613);unsigned queries=query_count("hidraw7");assert(queries==3);
    for(unsigned n=0;n<7;++n) packet("hidraw7",tick,2);
    remove_file("hidraw7.feature-fail");assert(pump(s,1,6,1300)==613);
    assert(query_count("hidraw7")>queries);
    for(unsigned n=0;n<7;++n) packet("hidraw7",tick,2);
    assert(pump(s,1,6,50)==613);packet("hidraw7",tick,2);expect(s,1,6,614);
    device_create("hidraw8",coarse,sizeof coarse);write_file("hidraw8.descriptor-fail",NULL,0);
    assert(pump(s,1,6,350)==614 && query_count("hidraw8")==1);
    packet("hidraw8",&one,1);remove_file("hidraw8.descriptor-fail");assert(pump(s,1,6,1300)==614);
    packet("hidraw8",&one,1);expect(s,1,6,615);
    /* Exhausted probe sockets recover without reconnecting or restarting the
     * reader, and healthy devices continue during resource pressure. */
    write_file("probe-socket-fail",NULL,0);device_create("hidraw9",coarse,sizeof coarse);
    assert(pump(s,1,6,350)==615);packet("hidraw0",&one,1);expect(s,1,6,616);
    packet("hidraw9",&one,1);remove_file("probe-socket-fail");assert(pump(s,1,6,1300)==616);
    packet("hidraw9",&one,1);expect(s,1,6,617);
    /* Unsupported descriptors are not repeatedly queried during scans. */
    uint8_t unrelated[sizeof coarse];memcpy(unrelated,coarse,sizeof unrelated);unrelated[4]=0;
    device_create("hidraw10",unrelated,sizeof unrelated);assert(pump(s,1,6,350)==617);
    queries=query_count("hidraw10");assert(queries==2);
    assert(pump(s,1,6,1300)==617 && query_count("hidraw10")==queries);
    /* A reader-level stall still uses the independent frontend watchdog. */
    previous=last_child;assert(!kill(previous,SIGSTOP));
    assert(pump(s,1,6,4650)==617 && last_child!=previous);
    packet("hidraw0",&one,1);expect(s,1,6,618);
    /* HIDraw opens/releases can wait on a kernel-wide lock held by another
     * device's feature GET. Neither operation may stop existing motion or
     * the reader heartbeat, even though O_NONBLOCK is set on the node. */
    device_create("hidraw11",coarse,sizeof coarse);write_file("hidraw11.block-open",NULL,0);
    previous=last_child;assert(pump(s,1,6,350)==618);
    packet("hidraw0",&one,1);expect(s,1,6,619);
    assert(pump(s,1,6,3500)==619 && last_child==previous);
    remove_file("hidraw11.block-open");assert(pump(s,1,6,3500)==619);
    packet("hidraw11",&one,1);expect(s,1,6,620);
    write_file("hidraw11.block-close",NULL,0);remove_file("hidraw11");
    assert(pump(s,1,6,350)==620);packet("hidraw0",&one,1);expect(s,1,6,621);
    assert(pump(s,1,6,3500)==621 && last_child==previous);
    remove_file("hidraw11.block-close");
    previous=last_child;tm_hid_wheel_close(s);
    for(unsigned n=0;n<100 && !kill(previous,0);++n) {usleep(5000);tm_hid_wheel_close(NULL);}
    assert(kill(previous,0)<0 && errno==ESRCH);
    tm_hid_wheel_close(NULL);
    assert(waitpid(-1,NULL,WNOHANG)<0 && errno==ECHILD);
}
static void kernel_startup(void)
{
    write_file(".kernel-lock",NULL,0);
    device_create("hidraw0",coarse,sizeof coarse);
    uint8_t feature[]={2,1};
    for(unsigned n=1;n<4;++n) {
        char name[32],field[64];snprintf(name,sizeof name,"hidraw%u",n);
        device_create(name,fine,sizeof fine);
        snprintf(field,sizeof field,"%s.feature-2",name);write_file(field,feature,sizeof feature);
    }
    write_file("hidraw3.block-feature",NULL,0);
    tm_hid_wheel *s=tm_hid_wheel_open(fixture);assert(s);
    uint64_t deadline=now()+15000;
    do {assert(!pump(s,1,0,20));assert(now()<deadline);}while(tm_hid_wheel_devices_ready(s)!=3);
    pid_t worker=last_child;
    uint8_t one=1,tick[]={1,1};packet("hidraw0",&one,1);expect(s,1,0,1);
    for(unsigned n=0;n<8;++n) {packet("hidraw1",tick,2);packet("hidraw2",tick,2);}
    expect(s,1,0,3);
    assert(pump(s,1,0,3500)==3 && last_child==worker);
    packet("hidraw0",&one,1);expect(s,1,0,4);
    tm_hid_wheel_close(s);
    for(unsigned n=0;n<100 && !kill(worker,0);++n) {usleep(5000);tm_hid_wheel_close(NULL);}
    assert(kill(worker,0)<0 && errno==ESRCH);
}
static void cleanup_fixture(void)
{
    for(unsigned n=0;n<pinned_count;++n) assert(!close(pinned_nodes[n]));
    pinned_count=0;
    DIR *dir=opendir(fixture);assert(dir);struct dirent *entry;
    while((entry=readdir(dir))) if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) remove_file(entry->d_name);
    closedir(dir);assert(!rmdir(fixture));
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--hid-wheel-worker")) {
        worker_process=1;
        assert(strlen(argv[2])<sizeof fixture);strcpy(fixture,argv[2]);
        return tm_hid_wheel_worker(argc,argv);
    }
    char directory[]="/tmp/tm-hid-wheel-XXXXXX";assert(mkdtemp(directory));strcpy(fixture,directory);
    lifecycle();
    cleanup_fixture();
    char kernel_directory[]="/tmp/tm-hid-kernel-XXXXXX";assert(mkdtemp(kernel_directory));strcpy(fixture,kernel_directory);
    kernel_startup();cleanup_fixture();
    puts("HID worker discovery, GET scaling, OSD gating, lifecycle, backpressure and watchdog checks passed");
    return 0;
}
