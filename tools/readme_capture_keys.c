#define _POSIX_C_SOURCE 200809L
/* Select Studio views for authentic screenshots without modifying a project.
 * Synthetic Linux keyboard events are released before device destruction. */
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
static int selected(void) {
    char name[80]={0}; FILE *f=fopen("/tmp/CORENAME","r"); if(!f) return 0;
    int ok=fgets(name,sizeof name,f)!=NULL; fclose(f);
    name[strcspn(name,"\r\n")]=0; return ok && !strcmp(name,"TIC-80");
}
static int pause_ms(unsigned ms) {
    for(unsigned n=0;n<ms;n+=10) {
        if(!selected()) return -1;
        struct timespec delay={0,10000000}; nanosleep(&delay,NULL);
    }
    return 0;
}
static int event(int fd,unsigned type,unsigned code,int value) {
    struct input_event e={0}; e.type=type;e.code=code;e.value=value;
    return write(fd,&e,sizeof e)==sizeof e?0:-1;
}
int main(int argc,char **argv) {
    if(argc!=2 || !selected()) return 2;
    unsigned key=0,ctrl=0;
    if(!strcmp(argv[1],"console")) key=KEY_ESC;
    else if(!strcmp(argv[1],"code")) key=KEY_F1;
    else if(!strcmp(argv[1],"sprite")) key=KEY_F2;
    else if(!strcmp(argv[1],"map")) key=KEY_F3;
    else if(!strcmp(argv[1],"music")) key=KEY_F5;
    else if(!strcmp(argv[1],"top")) {key=KEY_HOME;ctrl=KEY_LEFTCTRL;}
    else if(!strcmp(argv[1],"help")) key=KEY_H;
    else if(!strcmp(argv[1],"clear")) key=KEY_C;
    else if(!strcmp(argv[1],"run")) {key=KEY_R;ctrl=KEY_LEFTCTRL;}
    else return 2;
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK),created=0,result=1;
    if(fd<0) return 1;
    struct uinput_setup setup={0};setup.id.bustype=BUS_VIRTUAL;
    strcpy(setup.name,"TIC-80 README screenshot keyboard");
    if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0 || ioctl(fd,UI_SET_EVBIT,EV_SYN)<0 ||
       ioctl(fd,UI_SET_KEYBIT,key)<0 || (ctrl && ioctl(fd,UI_SET_KEYBIT,ctrl)<0) ||
       ioctl(fd,UI_SET_KEYBIT,KEY_E)<0 || ioctl(fd,UI_SET_KEYBIT,KEY_L)<0 ||
       ioctl(fd,UI_SET_KEYBIT,KEY_P)<0 || ioctl(fd,UI_SET_KEYBIT,KEY_ENTER)<0 ||
       ioctl(fd,UI_SET_KEYBIT,KEY_S)<0 ||
       ioctl(fd,UI_DEV_SETUP,&setup)<0 || ioctl(fd,UI_DEV_CREATE)<0) goto done;
    created=1;
    if(pause_ms(3000)) goto done;
    if((ctrl && event(fd,EV_KEY,ctrl,1)) || event(fd,EV_KEY,key,1) ||
       event(fd,EV_SYN,SYN_REPORT,0) || pause_ms(120)) goto done;
    event(fd,EV_KEY,key,0);if(ctrl) event(fd,EV_KEY,ctrl,0);event(fd,EV_SYN,SYN_REPORT,0);
    if(!strcmp(argv[1],"help") || !strcmp(argv[1],"clear")) {
        const unsigned help[]={KEY_E,KEY_L,KEY_P,KEY_ENTER},clear[]={KEY_L,KEY_S,KEY_ENTER};
        const unsigned *rest=!strcmp(argv[1],"help")?help:clear;
        unsigned count=!strcmp(argv[1],"help")?4:3;
        for(unsigned i=0;i<count;++i) {
            if(pause_ms(150) || event(fd,EV_KEY,rest[i],1) || event(fd,EV_SYN,SYN_REPORT,0) || pause_ms(100)) goto done;
            event(fd,EV_KEY,rest[i],0);event(fd,EV_SYN,SYN_REPORT,0);
        }
    }
    if(pause_ms(600)) goto done;
    result=0;
done:
    event(fd,EV_KEY,key,0);if(ctrl) event(fd,EV_KEY,ctrl,0);event(fd,EV_SYN,SYN_REPORT,0);
    if(created) ioctl(fd,UI_DEV_DESTROY);
    close(fd);return result;
}
