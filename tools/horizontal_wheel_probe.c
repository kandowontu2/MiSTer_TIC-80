#define _POSIX_C_SOURCE 200809L
/* Synthetic Linux input probe. Each checkpoint waits for the external reader
 * to verify the cartridge's actual pmem output before more events are emitted.
 * Core generation changes terminate input immediately. No DDR writes occur.
 * Expected cartridge signs follow pinned Linux Studio (right is negative),
 * independently of Main's positive-right evdev counter. */
#include <linux/uinput.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef REL_HWHEEL_HI_RES
#define REL_HWHEEL_HI_RES 0x0c
#endif
static volatile sig_atomic_t stopping;
static struct stat generation;
static const char *directory;
static unsigned phase;
static int coarse=-1,fine=-1,keyboard=-1;
static void stop(int signum) { (void)signum; stopping=1; }
static int selected(void)
{
    struct stat s; char name[80]={0};
    FILE *f=fopen("/tmp/CORENAME","r"); if(!f) return 0;
    int ok=fgets(name,sizeof name,f)!=NULL; fclose(f);
    name[strcspn(name,"\r\n")]=0;
    return ok && !strcmp(name,"TIC-80") && !stopping && !stat("/tmp/CORENAME",&s) &&
        s.st_dev==generation.st_dev && s.st_ino==generation.st_ino &&
        s.st_mtim.tv_sec==generation.st_mtim.tv_sec && s.st_mtim.tv_nsec==generation.st_mtim.tv_nsec &&
        s.st_ctim.tv_sec==generation.st_ctim.tv_sec && s.st_ctim.tv_nsec==generation.st_ctim.tv_nsec;
}
static int pause_ms(unsigned ms)
{
    for(unsigned n=0;n<ms;n+=25) {
        if(!selected()) return -1;
        struct timespec t={0,25000000}; nanosleep(&t,NULL);
    }
    return selected()?0:-1;
}
static int event(int fd,unsigned type,unsigned code,int value)
{
    if(!selected()) return -1;
    struct input_event e={0}; e.type=type;e.code=code;e.value=value;
    ssize_t n; do n=write(fd,&e,sizeof e); while(n<0 && errno==EINTR && !stopping);
    return n==sizeof e?0:-1;
}
static int frame(int fd,unsigned code,int value)
{ return event(fd,EV_REL,code,value)||event(fd,EV_SYN,SYN_REPORT,0)?-1:0; }
static int create_device(int mode)
{
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC); if(fd<0) return -1;
    struct uinput_setup s={0}; s.id.bustype=BUS_VIRTUAL;
    snprintf(s.name,sizeof s.name,"TIC-80 horizontal diagnostic %s",mode==2?"keys":mode?"high-resolution":"coarse");
    if(ioctl(fd,UI_SET_EVBIT,EV_SYN)<0 || ioctl(fd,UI_SET_EVBIT,EV_KEY)<0) goto fail;
    if(mode==2) {
        if(ioctl(fd,UI_SET_KEYBIT,KEY_LEFTMETA)<0 || ioctl(fd,UI_SET_KEYBIT,KEY_F12)<0) goto fail;
    } else {
        if(ioctl(fd,UI_SET_KEYBIT,BTN_LEFT)<0 || ioctl(fd,UI_SET_EVBIT,EV_REL)<0 ||
            ioctl(fd,UI_SET_RELBIT,REL_X)<0 || ioctl(fd,UI_SET_RELBIT,REL_Y)<0 ||
            ioctl(fd,UI_SET_RELBIT,REL_WHEEL)<0 || ioctl(fd,UI_SET_RELBIT,REL_HWHEEL)<0 ||
            (mode && ioctl(fd,UI_SET_RELBIT,REL_HWHEEL_HI_RES)<0)) goto fail;
    }
    if(ioctl(fd,UI_DEV_SETUP,&s)<0 || ioctl(fd,UI_DEV_CREATE)<0) goto fail;
    return fd;
fail:
    close(fd);return -1;
}
static void destroy(int *fd)
{ if(*fd>=0) { ioctl(*fd,UI_DEV_DESTROY);close(*fd);*fd=-1; } }
static int checkpoint(const char *label,int total,int positive,int negative)
{
    char path[4096],temporary[4096]; ++phase;
    if(snprintf(path,sizeof path,"%s/phase.json",directory)>=(int)sizeof path ||
       snprintf(temporary,sizeof temporary,"%s/phase.tmp",directory)>=(int)sizeof temporary) return -1;
    FILE *f=fopen(temporary,"w"); if(!f) return -1;
    int n=fprintf(f,"{\"phase\":%u,\"label\":\"%s\",\"total\":%d,\"positive\":%d,\"negative\":%d}\n",
        phase,label,total,positive,negative);
    int closed=fclose(f); if(n<0 || closed || rename(temporary,path)) return -1;
    printf("Phase %u: %s total=%d positive=%d negative=%d\n",phase,label,total,positive,negative);fflush(stdout);
    if(snprintf(path,sizeof path,"%s/ack",directory)>=(int)sizeof path) return -1;
    for(unsigned ms=0;ms<20000;ms+=25) {
        unsigned ack=0; f=fopen(path,"r");
        if(f) { int got=fscanf(f,"%u",&ack);fclose(f);if(got==1 && ack==phase) return pause_ms(100); }
        if(pause_ms(25)) return -1;
    }
    fprintf(stderr,"Checkpoint %u observation was not acknowledged\n",phase);return -1;
}
static int menu_toggle(void)
{
    if(event(keyboard,EV_KEY,KEY_LEFTMETA,1)||event(keyboard,EV_KEY,KEY_F12,1)||
       event(keyboard,EV_SYN,SYN_REPORT,0)||pause_ms(150)||
       event(keyboard,EV_KEY,KEY_F12,0)||event(keyboard,EV_KEY,KEY_LEFTMETA,0)||
       event(keyboard,EV_SYN,SYN_REPORT,0)) return -1;
    return pause_ms(500);
}
#define DO(e) do { if((e)<0) goto done; } while(0)
int main(int argc,char **argv)
{
    if(argc!=2 || argv[1][0]!='/') { fprintf(stderr,"Usage: horizontal-wheel-probe /private/checkpoint/directory\n");return 2; }
    directory=argv[1]; signal(SIGINT,stop); signal(SIGTERM,stop);
    if(stat("/tmp/CORENAME",&generation)||!selected()) return 2;
    int result=1;
    coarse=create_device(0); fine=create_device(1); keyboard=create_device(2);
    if(coarse<0 || fine<0 || keyboard<0) { perror("uinput device");goto done; }
    DO(pause_ms(3000)); DO(checkpoint("initial idle",0,0,0));
    DO(frame(coarse,REL_HWHEEL,3)); DO(checkpoint("coarse right",-3,0,3));
    DO(frame(coarse,REL_HWHEEL,-5)); DO(checkpoint("coarse left",2,5,3));
    DO(frame(fine,REL_HWHEEL_HI_RES,30)); DO(checkpoint("quarter detent",2,5,3));
    DO(frame(fine,REL_HWHEEL,1)); DO(checkpoint("separate coarse companion",2,5,3));
    DO(frame(coarse,REL_HWHEEL,2)); DO(checkpoint("independent coarse device",0,5,5));
    DO(frame(fine,REL_HWHEEL_HI_RES,90)); DO(checkpoint("completed fine detent",-1,5,6));
    DO(frame(fine,REL_HWHEEL_HI_RES,-250)); DO(checkpoint("fine left with remainder",1,7,6));
    DO(frame(fine,REL_HWHEEL_HI_RES,-110)); DO(checkpoint("completed negative remainder",2,8,6));
    DO(frame(coarse,REL_HWHEEL,65)); DO(checkpoint("positive runtime backlog",-63,8,71));
    DO(frame(coarse,REL_HWHEEL,-63)); DO(checkpoint("negative runtime backlog",0,71,71));
    DO(frame(fine,REL_HWHEEL_HI_RES,60)); DO(checkpoint("partial before disconnect",0,71,71));
    destroy(&fine); DO(pause_ms(500)); fine=create_device(1); if(fine<0) goto done;
    DO(pause_ms(3000)); DO(checkpoint("device reconnected",0,71,71));
    DO(frame(fine,REL_HWHEEL_HI_RES,60)); DO(checkpoint("new device partial",0,71,71));
    DO(frame(fine,REL_HWHEEL_HI_RES,60)); DO(checkpoint("new device complete",-1,71,72));
    DO(frame(fine,REL_HWHEEL_HI_RES,60)); DO(checkpoint("partial before OSD",-1,71,72));
    DO(menu_toggle()); DO(checkpoint("OSD opened",-1,71,72));
    DO(frame(fine,REL_HWHEEL_HI_RES,120)); DO(checkpoint("wheel suppressed in OSD",-1,71,72));
    DO(menu_toggle()); DO(checkpoint("OSD closed",-1,71,72));
    DO(frame(fine,REL_HWHEEL_HI_RES,60)); DO(checkpoint("discarded pre-OSD remainder",-1,71,72));
    DO(frame(fine,REL_HWHEEL_HI_RES,60)); DO(checkpoint("post-OSD detent",-2,71,73));
    DO(pause_ms(500)); DO(checkpoint("final idle",-2,71,73));
    puts("Horizontal input probe passed all 22 acknowledged checkpoints");result=0;
done:
    destroy(&keyboard);destroy(&fine);destroy(&coarse);
    if(result) fprintf(stderr,"Horizontal input probe aborted; no further events emitted\n");
    return result;
}
