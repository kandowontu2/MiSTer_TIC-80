#define _POSIX_C_SOURCE 200809L
/* Development-only end-to-end probe. Emits through Linux uinput so MiSTer's
 * normal device discovery, key translation and HPS transport are exercised.
 * Refuses to create devices or emit events while another core is selected. */
#include <linux/uinput.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tic80_mister/backend.h"
static volatile sig_atomic_t stopping;
static void stop(int signal_number) { (void)signal_number; stopping=1; }
static int selected(void)
{
    char name[80]={0};
    FILE *file=fopen("/tmp/CORENAME","r");
    if(!file) return 0;
    int read=fgets(name,sizeof name,file)!=NULL;
    fclose(file);
    name[strcspn(name,"\r\n")]=0;
    return read && !strcmp(name,"TIC-80") && !stopping;
}
static int pause_ms(unsigned milliseconds)
{
    for(unsigned elapsed=0;elapsed<milliseconds;elapsed+=50) {
        if(!selected()) return -1;
        struct timespec time={0,50000000};
        nanosleep(&time,NULL);
    }
    return selected()?0:-1;
}
static int event(int fd,unsigned type,unsigned code,int value)
{
    if(!selected()) return -1;
    struct input_event event={0};
    event.type=type;event.code=code;event.value=value;
    ssize_t written;
    do written=write(fd,&event,sizeof event);while(written<0 && errno==EINTR && !stopping);
    return written==sizeof event?0:-1;
}
static int sync_device(int fd) { return event(fd,EV_SYN,SYN_REPORT,0); }
static int device(int mouse)
{
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK);
    if(fd<0) return -1;
    struct uinput_setup setup={0};
    setup.id.bustype=BUS_VIRTUAL;
    snprintf(setup.name,sizeof setup.name,"TIC-80 validation %s",mouse?"mouse":"keyboard");
    if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0 || ioctl(fd,UI_SET_EVBIT,EV_SYN)<0) goto fail;
    if(mouse) {
        if(ioctl(fd,UI_SET_EVBIT,EV_REL)<0 || ioctl(fd,UI_SET_RELBIT,REL_X)<0 ||
                ioctl(fd,UI_SET_RELBIT,REL_Y)<0 || ioctl(fd,UI_SET_RELBIT,REL_WHEEL)<0 ||
                ioctl(fd,UI_SET_KEYBIT,BTN_LEFT)<0 || ioctl(fd,UI_SET_KEYBIT,BTN_RIGHT)<0 ||
                ioctl(fd,UI_SET_KEYBIT,BTN_MIDDLE)<0) goto fail;
    } else for(unsigned code=1;code<256;++code) if(ioctl(fd,UI_SET_KEYBIT,code)<0) goto fail;
    if(ioctl(fd,UI_DEV_SETUP,&setup)<0 || ioctl(fd,UI_DEV_CREATE)<0) goto fail;
    return fd;
fail:
    close(fd);return -1;
}
#define DO(call) do { if((call)<0) goto done; } while(0)
int main(int argc,char **argv)
{
    int tetris=argc==2 && !strcmp(argv[1],"--tetris");
    if(argc!=1 && !tetris) { fprintf(stderr,"Usage: %s [--tetris]\n",argv[0]);return 2; }
    signal(SIGINT,stop);signal(SIGTERM,stop);
    if(!selected()) { fprintf(stderr,"TIC-80 must be selected before running the input probe\n");return 2; }
    tm_backend core;
    if(tm_backend_open(&core,NULL)) return 2; // live matching protocol, read only
    tm_backend_close(&core);
    int keyboard=device(0), mouse=-1, result=1;
    if(keyboard<0) { perror("uinput keyboard");return 1; }
    mouse=device(1);
    if(mouse<0) { perror("uinput mouse");goto done; }
    DO(pause_ms(3000)); // bounded time for MiSTer to discover the new devices
    if(tetris) {
        puts("Probe: keyboard Z starts Tetris; Right moves the active piece");fflush(stdout);
        DO(event(keyboard,EV_KEY,KEY_Z,1));DO(sync_device(keyboard));DO(pause_ms(200));
        DO(event(keyboard,EV_KEY,KEY_Z,0));DO(sync_device(keyboard));DO(pause_ms(200));
        DO(event(keyboard,EV_KEY,KEY_RIGHT,1));DO(sync_device(keyboard));DO(pause_ms(200));
        DO(event(keyboard,EV_KEY,KEY_RIGHT,0));DO(sync_device(keyboard));DO(pause_ms(1000));
        result=0;goto done;
    }
    puts("Probe: A, both Ctrl keys, all mouse buttons and wheel");fflush(stdout);
    DO(event(keyboard,EV_KEY,KEY_A,1));DO(event(keyboard,EV_KEY,KEY_LEFTCTRL,1));
    DO(event(keyboard,EV_KEY,KEY_RIGHTCTRL,1));DO(sync_device(keyboard));
    DO(event(mouse,EV_REL,REL_X,12));DO(event(mouse,EV_REL,REL_Y,7));
    DO(event(mouse,EV_REL,REL_WHEEL,3));
    DO(event(mouse,EV_KEY,BTN_LEFT,1));DO(event(mouse,EV_KEY,BTN_MIDDLE,1));
    DO(event(mouse,EV_KEY,BTN_RIGHT,1));DO(sync_device(mouse));DO(pause_ms(500));
    DO(event(keyboard,EV_KEY,KEY_LEFTCTRL,0));DO(sync_device(keyboard));DO(pause_ms(500));
    DO(event(keyboard,EV_KEY,KEY_RIGHTCTRL,0));DO(event(keyboard,EV_KEY,KEY_A,0));DO(sync_device(keyboard));
    DO(event(mouse,EV_KEY,BTN_LEFT,0));DO(event(mouse,EV_KEY,BTN_MIDDLE,0));
    DO(event(mouse,EV_KEY,BTN_RIGHT,0));DO(sync_device(mouse));
    DO(event(keyboard,EV_KEY,KEY_F12,1));DO(sync_device(keyboard));DO(pause_ms(100));
    DO(event(keyboard,EV_KEY,KEY_F12,0));DO(sync_device(keyboard));DO(pause_ms(200));
    puts("Probe: opening/closing MiSTer menu while A and mouse-left are held");fflush(stdout);
    DO(event(keyboard,EV_KEY,KEY_A,1));DO(sync_device(keyboard));
    DO(event(mouse,EV_KEY,BTN_LEFT,1));DO(sync_device(mouse));DO(pause_ms(200));
    DO(event(keyboard,EV_KEY,KEY_LEFTMETA,1));DO(event(keyboard,EV_KEY,KEY_F12,1));DO(sync_device(keyboard));DO(pause_ms(300));
    DO(event(keyboard,EV_KEY,KEY_A,0));DO(event(keyboard,EV_KEY,KEY_F12,0));
    DO(event(keyboard,EV_KEY,KEY_LEFTMETA,0));DO(sync_device(keyboard));
    DO(event(mouse,EV_KEY,BTN_LEFT,0));DO(sync_device(mouse));DO(pause_ms(300));
    DO(event(keyboard,EV_KEY,KEY_LEFTMETA,1));DO(event(keyboard,EV_KEY,KEY_F12,1));DO(sync_device(keyboard));DO(pause_ms(100));
    DO(event(keyboard,EV_KEY,KEY_F12,0));DO(event(keyboard,EV_KEY,KEY_LEFTMETA,0));DO(sync_device(keyboard));DO(pause_ms(500));
    DO(event(mouse,EV_REL,REL_X,-4));DO(event(mouse,EV_REL,REL_Y,2));
    DO(event(mouse,EV_REL,REL_WHEEL,-1));DO(sync_device(mouse));DO(pause_ms(1500));
    puts("Probe events complete; inspect cartridge results and confirm no stuck input");
    result=0;
done:
    if(mouse>=0) { ioctl(mouse,UI_DEV_DESTROY);close(mouse); }
    ioctl(keyboard,UI_DEV_DESTROY);close(keyboard);
    if(result) fprintf(stderr,"Input probe interrupted, core changed, or device operation failed\n");
    return result;
}
