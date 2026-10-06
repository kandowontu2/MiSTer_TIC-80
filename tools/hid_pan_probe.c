#define _GNU_SOURCE
/* Development-only UHID transport probe. Creates only private test mice, and
 * refuses creation/injection outside TIC-80. The real HID kernel boundary,
 * production private reader and stock Main's evdev ownership are exercised.
 * This does not qualify the FPGA gate or the frontend TIC mouse() API. */
#include "tic80_mister/hid_pan.h"
#include "tic80_mister/hid_wheel.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <linux/uhid.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int uhid, evdev, fine, numbered, blocked, gets, sets;
    char name[128], raw_path[128], event_path[128], alias[256];
} mouse;
static mouse devices[4];
static volatile sig_atomic_t stopping;
static pid_t main_pid;
static char directory[128];
static tm_hid_wheel *reader;
static void stop(int signal_number) { (void)signal_number; stopping=1; }
static uint64_t now(void)
{
    struct timespec time; clock_gettime(CLOCK_MONOTONIC,&time);
    return (uint64_t)time.tv_sec*1000+(uint64_t)time.tv_nsec/1000000;
}
static int selected(void)
{
    char name[80]={0}; FILE *file=fopen("/tmp/CORENAME","r");
    if(!file) return 0;
    int ok=fgets(name,sizeof name,file)!=NULL; fclose(file);
    name[strcspn(name,"\r\n")]=0;
    return ok && !strcmp(name,"TIC-80") && !stopping;
}
static size_t descriptor(uint8_t *bytes,int fine,int numbered)
{
    const uint8_t collections[]={5,1,9,2,0xa1,1,9,1,0xa1,0,0xa1,2};
    const uint8_t multiplier[]={5,1,9,0x48,0x15,0,0x25,1,0x35,1,0x45,8,0x75,8,0x95,1,0xb1,2};
    const uint8_t inputs[]={5,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,
        0x75,5,0x95,1,0x81,1,5,1,9,0x30,9,0x31,9,0x38,0x15,0x81,0x25,0x7f,
        0x75,8,0x95,3,0x81,6,5,0x0c,0x0a,0x38,2,0x95,1,0x81,6,0xc0,0xc0,0xc0};
    size_t length=0;
    memcpy(bytes+length,collections,sizeof collections);length+=sizeof collections;
    if(fine) {
        if(numbered) {bytes[length++]=0x85;bytes[length++]=2;}
        memcpy(bytes+length,multiplier,sizeof multiplier);length+=sizeof multiplier;
    }
    if(numbered) {bytes[length++]=0x85;bytes[length++]=1;}
    memcpy(bytes+length,inputs,sizeof inputs);return length+sizeof inputs;
}
static int self_test(void)
{
    for(unsigned mode=0;mode<3;++mode) {
        uint8_t bytes[256],feature=1,report[6]={0};tm_hid_pan pan;int64_t delta;
        int fine=mode!=0,numbered=mode==1;
        size_t size=descriptor(bytes,fine,numbered);
        if(tm_hid_pan_parse(&pan,bytes,size)!=1 || pan.count!=1) return 1;
        if(fine && tm_hid_pan_feature(&pan,numbered?2:0,&feature,1)!=1) return 1;
        if(!tm_hid_pan_ready(&pan)) return 1;
        if(numbered) report[0]=1;
        report[4+numbered]=1;
        for(unsigned n=0;n<(fine?8u:1u);++n) {
            if(tm_hid_pan_input(&pan,report,5+numbered,&delta)!=1 ||
               delta!=(n==(fine?7u:0u)?1:0)) return 1;
        }
    }
    printf("{\"self_test\":true,\"uhid_event_size\":%zu,\"create2_size\":%zu}\n",
           sizeof(struct uhid_event),sizeof(struct uhid_create2_req));
    return 0;
}
static int write_event(mouse *m,struct uhid_event *event)
{
    if(!selected()) return -1;
    ssize_t size;
    do size=write(m->uhid,event,sizeof *event);while(size<0 && errno==EINTR && !stopping);
    return size==sizeof *event?0:-1;
}
static int create(mouse *m,unsigned index)
{
    if(!selected()) return -1;
    m->uhid=open("/dev/uhid",O_RDWR|O_NONBLOCK|O_CLOEXEC);
    if(m->uhid<0) return -1;
    snprintf(m->name,sizeof m->name,"TIC-80 native pan %ld %u",(long)getpid(),index);
    struct uhid_event event={0};event.type=UHID_CREATE2;
    snprintf((char *)event.u.create2.name,sizeof event.u.create2.name,"%s",m->name);
    snprintf((char *)event.u.create2.uniq,sizeof event.u.create2.uniq,"tic80-pan-%ld-%u",(long)getpid(),index);
    event.u.create2.rd_size=(uint16_t)descriptor(event.u.create2.rd_data,m->fine,m->numbered);
    event.u.create2.bus=BUS_USB;event.u.create2.vendor=0xffff;event.u.create2.product=0x0080+index;
    if(write_event(m,&event)) return -1;
    return 0;
}
static int service(mouse *m)
{
    for(unsigned n=0;n<64;++n) {
        struct uhid_event event={0};ssize_t size=read(m->uhid,&event,sizeof event);
        if(size<0 && (errno==EAGAIN || errno==EINTR)) return 0;
        if(size<(ssize_t)sizeof event.type) return -1;
        if(event.type==UHID_GET_REPORT) {
            ++m->gets;
            if(m->blocked) continue;
            struct uhid_event answer={0};answer.type=UHID_GET_REPORT_REPLY;
            answer.u.get_report_reply.id=event.u.get_report.id;
            if(!m->fine || event.u.get_report.rtype!=UHID_FEATURE_REPORT ||
               event.u.get_report.rnum!=(m->numbered?2:0)) answer.u.get_report_reply.err=EIO;
            else {
                unsigned prefix=m->numbered?1:0;
                if(prefix) answer.u.get_report_reply.data[0]=2;
                answer.u.get_report_reply.data[prefix]=1;
                answer.u.get_report_reply.size=(uint16_t)(prefix+1);
            }
            if(write_event(m,&answer)) return -1;
        } else if(event.type==UHID_SET_REPORT) {
            /* Kernel setup may SET a multiplier. Keep this virtual fixture's
             * current feature fixed at scale eight; production only GETs it. */
            ++m->sets;struct uhid_event answer={0};answer.type=UHID_SET_REPORT_REPLY;
            answer.u.set_report_reply.id=event.u.set_report.id;
            answer.u.set_report_reply.err=EIO;
            if(write_event(m,&answer)) return -1;
        }
    }
    return 0;
}
static int discover(mouse *m,unsigned index)
{
    DIR *dir=opendir("/dev");if(!dir) return -1;struct dirent *entry;
    while((entry=readdir(dir))) {
        if(strncmp(entry->d_name,"hidraw",6)) continue;
        char path[128],name[128]={0};snprintf(path,sizeof path,"/dev/%.100s",entry->d_name);
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0) continue;
        int matches=ioctl(fd,HIDIOCGRAWNAME(sizeof name),name)>=0 && !strcmp(name,m->name);
        struct stat metadata;int valid=!fstat(fd,&metadata);close(fd);
        if(matches && valid && S_ISCHR(metadata.st_mode)) {
            snprintf(m->raw_path,sizeof m->raw_path,"%s",path);
            snprintf(m->alias,sizeof m->alias,"%s/hidraw%u",directory,index);
            if(mknod(m->alias,S_IFCHR|0600,metadata.st_rdev)) {closedir(dir);return -1;}
            break;
        }
    }
    closedir(dir);
    dir=opendir("/dev/input");if(!dir) return -1;
    while((entry=readdir(dir))) {
        if(strncmp(entry->d_name,"event",5)) continue;
        char path[128],name[128]={0};snprintf(path,sizeof path,"/dev/input/%.100s",entry->d_name);
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0) continue;
        if(ioctl(fd,EVIOCGNAME(sizeof name),name)>=0 && !strcmp(name,m->name)) {
            m->evdev=fd;snprintf(m->event_path,sizeof m->event_path,"%s",path);break;
        }
        close(fd);
    }
    closedir(dir);return 0;
}
static int main_holds(mouse *m)
{
    if(m->evdev<0) return 0;
    struct stat wanted;if(fstat(m->evdev,&wanted)) return 0;
    char path[128];snprintf(path,sizeof path,"/proc/%ld/fd",(long)main_pid);
    DIR *dir=opendir(path);if(!dir) return 0;struct dirent *entry;int opened=0;
    while((entry=readdir(dir))) {
        struct stat actual;
        if(!fstatat(dirfd(dir),entry->d_name,&actual,0) && S_ISCHR(actual.st_mode) &&
            actual.st_rdev==wanted.st_rdev) {opened=1;break;}
    }
    closedir(dir);if(!opened) return 0;
    if(!ioctl(m->evdev,EVIOCGRAB,1)) {ioctl(m->evdev,EVIOCGRAB,0);return 0;}
    if(errno!=EBUSY) return 0;
    /* EBUSY alone does not identify the exclusive owner. For our newly
     * created virtual mouse, Main and this failed-grab observer must be the
     * only processes with evdev handles. Fail closed on inaccessible fd
     * inventories, tolerating processes/fds which vanish during enumeration. */
    dir=opendir("/proc");if(!dir) return 0;
    int unique=1;
    while((entry=readdir(dir))) {
        char *end;long pid=strtol(entry->d_name,&end,10);
        if(!*entry->d_name || *end || pid<=0 || pid==main_pid || pid==getpid()) continue;
        snprintf(path,sizeof path,"/proc/%ld/fd",pid);
        DIR *fds=opendir(path);
        if(!fds) {if(errno!=ENOENT && errno!=ESRCH) unique=0;continue;}
        struct dirent *fd;
        while((fd=readdir(fds))) {
            struct stat actual;
            if(!fstatat(dirfd(fds),fd->d_name,&actual,0)) {
                if(S_ISCHR(actual.st_mode) && actual.st_rdev==wanted.st_rdev) unique=0;
            } else if(errno!=ENOENT && errno!=ESRCH) unique=0;
        }
        closedir(fds);
    }
    closedir(dir);return unique;
}
static int pump(unsigned milliseconds,int active,unsigned epoch,uint32_t *total)
{
    uint64_t end=now()+milliseconds;
    do {
        if(!selected()) return -1;
        for(unsigned n=0;n<4;++n) if(devices[n].uhid>=0 && service(&devices[n])) return -1;
        if(reader) *total=tm_hid_wheel_poll(reader,now(),active,epoch);
        usleep(5000);
    } while(now()<end);
    return 0;
}
static int emit(mouse *m,int pan)
{
    struct uhid_event event={0};event.type=UHID_INPUT2;
    event.u.input2.size=(uint16_t)(5+m->numbered);
    if(m->numbered) event.u.input2.data[0]=1;
    event.u.input2.data[4+m->numbered]=(uint8_t)pan;
    return write_event(m,&event);
}
static int expect(uint32_t expected,int active,unsigned epoch)
{
    uint64_t deadline=now()+2000;uint32_t total=0;
    do {if(pump(10,active,epoch,&total)) return -1;}while(total!=expected && now()<deadline);
    if(total!=expected) {fprintf(stderr,"native pan %u != %u\n",total,expected);return -1;}
    if(pump(40,active,epoch,&total) || total!=expected) return -1;
    return 0;
}
#define CHECK(call) do {if((call)) {fprintf(stderr,"Probe failed at line %d: %s (errno=%d)\n",__LINE__,#call,errno);goto cleanup;}} while(0)
static int native(void)
{
    uint32_t total=0;int result=1;
    if(!selected()) {fprintf(stderr,"TIC-80 must be selected before this probe\n");return 2;}
    char main_path[128],main_name[32]={0};
    snprintf(main_path,sizeof main_path,"/proc/%ld/comm",(long)main_pid);
    FILE *main_file=fopen(main_path,"r");
    if(!main_file) return 2;
    int read_name=fgets(main_name,sizeof main_name,main_file)!=NULL;fclose(main_file);
    main_name[strcspn(main_name,"\r\n")]=0;
    if(!read_name || strcmp(main_name,"MiSTer")) return 2;
    snprintf(directory,sizeof directory,"/tmp/tic80-hid-pan-%ld-XXXXXX",(long)getpid());
    if(!mkdtemp(directory)) return 1;
    for(unsigned n=0;n<4;++n) {
        devices[n].uhid=devices[n].evdev=-1;devices[n].fine=n!=0;devices[n].numbered=n==1 || n==3;
    }
    for(unsigned n=0;n<4;++n) CHECK(create(&devices[n],n));
    uint64_t deadline=now()+15000;
    for(unsigned n=0;n<4;++n) {
        while(!*devices[n].raw_path || devices[n].evdev<0) {
            CHECK(pump(30,0,0,&total));
            /* Each discovery pass may find just one of raw and evdev. */
            if(!*devices[n].raw_path && devices[n].evdev<0) CHECK(discover(&devices[n],n));
            else {
                /* Retry safely without recreating an alias or retaining an
                 * extra evdev fd from the previous partial discovery. */
                if(devices[n].evdev>=0) {close(devices[n].evdev);devices[n].evdev=-1;}
                if(*devices[n].alias) {unlink(devices[n].alias);devices[n].alias[0]=0;}
                devices[n].raw_path[0]=0;CHECK(discover(&devices[n],n));
            }
            if(now()>deadline) {fprintf(stderr,"UHID device discovery timeout\n");goto cleanup;}
        }
        while(!main_holds(&devices[n])) {
            CHECK(pump(20,0,0,&total));
            if(now()>deadline) {fprintf(stderr,"Stock Main did not grab %s\n",devices[n].event_path);goto cleanup;}
        }
        printf("{\"main_pid\":%ld,\"event\":\"%s\",\"raw\":\"%s\",\"main_fd_verified\":true,\"evdev_grab_busy\":true,\"only_main_and_probe_evdev_fds\":true}\n",
               (long)main_pid,devices[n].event_path,devices[n].raw_path);fflush(stdout);
    }
    /* This virtual device already registered normally. Stall only its next
     * feature GET, made by the production private reader's per-device probe. */
    CHECK(pump(100,0,0,&total));
    int earlier_sets[4];
    for(unsigned n=0;n<4;++n) earlier_sets[n]=devices[n].sets;
    int earlier_gets=devices[3].gets;devices[3].blocked=1;
    reader=tm_hid_wheel_open(directory);if(!reader) goto cleanup;
    deadline=now()+15000;
    do {
        CHECK(pump(20,1,0,&total));
        if(now()>deadline) {fprintf(stderr,"Healthy HID devices did not become ready: %u\n",tm_hid_wheel_devices_ready(reader));goto cleanup;}
    } while(tm_hid_wheel_devices_ready(reader)!=3);
    CHECK(total!=0);
    CHECK(emit(&devices[0],1));CHECK(expect(1,1,0));
    CHECK(emit(&devices[0],-1));CHECK(expect(0,1,0));
    for(unsigned n=0;n<7;++n) {CHECK(emit(&devices[1],1));CHECK(emit(&devices[2],1));}
    CHECK(pump(100,1,0,&total));CHECK(total!=0);
    CHECK(emit(&devices[1],1));CHECK(emit(&devices[2],1));CHECK(expect(2,1,0));
    CHECK(pump(3500,1,0,&total));CHECK(total!=2);CHECK(devices[3].gets<=earlier_gets);
    CHECK(emit(&devices[0],1));CHECK(expect(3,1,0));
    CHECK(pump(100,0,1,&total));
    for(unsigned n=0;n<8;++n) {CHECK(emit(&devices[0],1));CHECK(emit(&devices[1],1));}
    CHECK(pump(100,0,1,&total));CHECK(total!=3);CHECK(expect(3,1,2));
    CHECK(emit(&devices[0],1));CHECK(expect(4,1,2));
    CHECK(pump(40,1,2,&total));CHECK(total!=4);
    for(unsigned n=0;n<4;++n) {
        CHECK(!main_holds(&devices[n]));CHECK(devices[n].sets!=earlier_sets[n]);
    }
    printf("{\"native_reader_passed\":true,\"total\":%u,\"blocked_feature_gets\":%d,\"scope\":\"Real UHID/HIDraw delivery with stock Main evdev grab; FPGA/frontend API qualification separate\"}\n",
           total,devices[3].gets-earlier_gets);fflush(stdout);result=0;
cleanup:
    tm_hid_wheel_close(reader);reader=NULL;
    for(unsigned n=0;n<4;++n) {
        if(devices[n].evdev>=0) close(devices[n].evdev);
        if(devices[n].uhid>=0) close(devices[n].uhid); /* UHID auto-destroys on close. */
        if(*devices[n].alias) unlink(devices[n].alias);
    }
    rmdir(directory);return result;
}
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--hid-wheel-worker")) return tm_hid_wheel_worker(argc,argv);
    if(argc==2 && !strcmp(argv[1],"--self-test")) return self_test();
    if(argc!=3 || strcmp(argv[1],"--native")) {
        fprintf(stderr,"Usage: %s --self-test | --native STOCK_MAIN_PID\n",argv[0]);return 2;
    }
    char *end;errno=0;long pid=strtol(argv[2],&end,10);
    if(errno || *end || pid<=1 || pid>INT32_MAX) return 2;
    main_pid=(pid_t)pid;
    signal(SIGINT,stop);signal(SIGTERM,stop);return native();
}
