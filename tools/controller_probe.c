#define _POSIX_C_SOURCE 200809L
/* Synthetic Xbox events through Main's normal remapper, HPS and DDR path.
 * This checks transport; it does not qualify a physical controller. */
#include "tic80_mister/backend.h"
#include "tic80_mister/memory_map.h"
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
static int selected(void)
{
    char name[80]={0}; FILE *f=fopen("/tmp/CORENAME","r"); if(!f) return 0;
    int ok=fgets(name,sizeof name,f)!=NULL; fclose(f);
    name[strcspn(name,"\r\n")]=0; return ok && !strcmp(name,"TIC-80");
}
static int pause_ms(unsigned ms)
{
    for(unsigned n=0;n<ms;n+=10) {
        if(!selected()) return -1;
        struct timespec delay={0,10000000}; nanosleep(&delay,NULL);
    }
    return 0;
}
static int emit(int fd,unsigned type,unsigned code,int value)
{
    if(!selected()) return -1;
    struct input_event e={0}; e.type=type; e.code=code; e.value=value;
    if(write(fd,&e,sizeof e)!=sizeof e) return -1;
    e=(struct input_event){0}; e.type=EV_SYN; e.code=SYN_REPORT;
    return write(fd,&e,sizeof e)==sizeof e?0:-1;
}
static int expect(tm_backend *b,unsigned raw)
{
    for(unsigned n=0;n<100;++n) {
        if(!selected()) return -1;
        tm_input_snapshot s;
        if(tm_backend_inputs(b,&s)<0) return -1;
        if((s.joystick[0]&0xffff)==raw) return 0;
        if(pause_ms(10)) return -1;
    }
    fprintf(stderr,"Controller raw mask did not reach 0x%x\n",raw); return -1;
}
#define DO(expr) do { if((expr)<0) goto done; } while(0)
int main(void)
{
    if(!selected()) return 2;
    tm_backend b; if(tm_backend_open(&b,NULL)) return 2;
    // Observe the service-owned session; never start a second producer.
    b.session=*(volatile unsigned *)(b.memory+TM_SESSION_ACK_OFFSET);
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK), result=1, created=0;
    if(fd<0) { perror("uinput"); b.session=0; tm_backend_close(&b); return 1; }
    struct uinput_setup setup={0}; setup.id.bustype=BUS_USB;
    setup.id.vendor=0x20d6; setup.id.product=0x2062; setup.id.version=0x0114;
    strcpy(setup.name,"TIC-80 validation Xbox profile");
    DO(ioctl(fd,UI_SET_EVBIT,EV_KEY)); DO(ioctl(fd,UI_SET_EVBIT,EV_ABS));
    static const unsigned buttons[]={BTN_A,BTN_B,BTN_X,BTN_Y,BTN_TL,BTN_TR,BTN_SELECT,BTN_START,BTN_MODE};
    for(unsigned i=0;i<sizeof buttons/sizeof *buttons;++i) DO(ioctl(fd,UI_SET_KEYBIT,buttons[i]));
    static const unsigned axes[]={ABS_X,ABS_Y,ABS_HAT0X,ABS_HAT0Y};
    for(unsigned i=0;i<4;++i) {
        DO(ioctl(fd,UI_SET_ABSBIT,axes[i]));
        struct uinput_abs_setup a={0}; a.code=axes[i];
        a.absinfo.minimum=i<2?-32768:-1; a.absinfo.maximum=i<2?32767:1;
        DO(ioctl(fd,UI_ABS_SETUP,&a));
    }
    DO(ioctl(fd,UI_DEV_SETUP,&setup)); DO(ioctl(fd,UI_DEV_CREATE)); created=1;
    DO(pause_ms(3000)); DO(emit(fd,EV_KEY,BTN_A,1)); DO(expect(&b,0x10));
    DO(emit(fd,EV_KEY,BTN_A,0)); DO(expect(&b,0)); DO(pause_ms(100));
    static const unsigned types[]={EV_ABS,EV_ABS,EV_ABS,EV_ABS,EV_KEY,EV_KEY,EV_KEY,EV_KEY};
    static const unsigned codes[]={ABS_Y,ABS_X,ABS_Y,ABS_X,BTN_START,BTN_SELECT,BTN_TL,BTN_TR};
    static const int values[]={-32768,-32768,32767,32767,1,1,1,1};
    for(unsigned action=0;action<8;++action) {
        DO(emit(fd,types[action],codes[action],values[action])); DO(expect(&b,1u<<(8+action)));
        DO(pause_ms(200)); DO(emit(fd,types[action],codes[action],0)); DO(expect(&b,0)); DO(pause_ms(100));
    }
    // D-pad right and stick up/left must remain independent, including diagonal.
    DO(emit(fd,EV_ABS,ABS_HAT0X,1)); DO(emit(fd,EV_ABS,ABS_X,-32768));
    DO(emit(fd,EV_ABS,ABS_Y,-32768)); DO(expect(&b,1|(1u<<8)|(1u<<9)));
    DO(pause_ms(200)); DO(emit(fd,EV_ABS,ABS_HAT0X,0));
    DO(emit(fd,EV_ABS,ABS_X,0)); DO(emit(fd,EV_ABS,ABS_Y,0)); DO(expect(&b,0));
    DO(pause_ms(200)); result=0;
    puts("Xbox remapper transport: WASD, Start/Enter, Back/Esc, L/Q, R/E, release and independent diagonal/D-pad passed");
done:
    if(created) ioctl(fd,UI_DEV_DESTROY); close(fd);
    b.session=0; // closing this observer must not clear the live service session
    tm_backend_close(&b); return result;
}
