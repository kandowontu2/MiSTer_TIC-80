#define _POSIX_C_SOURCE 200809L
/* Synthetic keyboard input through Linux/Main/HPS, restricted to a selected
 * TIC-80 core. This does not qualify a physical keyboard. */
#include <linux/uinput.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t stopping;
static void stop(int signal_number) { (void)signal_number; stopping=1; }
static int selected(void) {
    char name[80]={0}; FILE *f=fopen("/tmp/CORENAME","r"); if(!f) return 0;
    int ok=fgets(name,sizeof name,f)!=NULL; fclose(f);
    name[strcspn(name,"\r\n")]=0;
    return ok && !strcmp(name,"TIC-80") && !stopping;
}
static int pause_ms(unsigned milliseconds) {
    for(unsigned n=0;n<milliseconds;n+=10) {
        if(!selected()) return -1;
        struct timespec delay={0,10000000}; nanosleep(&delay,NULL);
    }
    return 0;
}
static int emit(int fd,unsigned type,unsigned code,int value) {
    if(!selected()) return -1;
    struct input_event e={0}; e.type=type; e.code=code; e.value=value;
    return write(fd,&e,sizeof e)==sizeof e?0:-1;
}
static int chord(int fd,unsigned first,unsigned second) {
    // Main can reorder keys from one Linux report. Establish modifiers before
    // their shortcut key, just as a physical keyboard normally does.
    if(second && (emit(fd,EV_KEY,first,1) || emit(fd,EV_SYN,SYN_REPORT,0) || pause_ms(50))) return -1;
    if((!second && emit(fd,EV_KEY,first,1)) || (second && emit(fd,EV_KEY,second,1)) ||
       emit(fd,EV_SYN,SYN_REPORT,0) || pause_ms(100)) return -1;
    if((second && emit(fd,EV_KEY,second,0)) || emit(fd,EV_KEY,first,0) ||
       emit(fd,EV_SYN,SYN_REPORT,0) || pause_ms(300)) return -1;
    return 0;
}
static int load_copy(int fd) {
    static const unsigned letters[]={KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,
        KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,
        KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    if(chord(fd,KEY_ESC,0)) return -1;
    const char *text="load validation.tic";
    for(;*text;++text) {
        unsigned key=*text==' '?KEY_SPACE:*text=='.'?KEY_DOT:letters[*text-'a'];
        if(chord(fd,key,0)) return -1;
    }
    return chord(fd,KEY_ENTER,0) || chord(fd,KEY_F1,0) ||
        chord(fd,KEY_LEFTCTRL,KEY_END) || chord(fd,KEY_SPACE,0) || chord(fd,KEY_LEFTCTRL,KEY_S);
}
int main(int argc,char **argv) {
    if(argc!=2 || (strcmp(argv[1],"edit") && strcmp(argv[1],"save") &&
       strcmp(argv[1],"cancel") && strcmp(argv[1],"yes") && strcmp(argv[1],"run") &&
       strcmp(argv[1],"escape") && strcmp(argv[1],"working-copy") && strcmp(argv[1],"load-copy"))) return 2;
    signal(SIGINT,stop); signal(SIGTERM,stop);
    if(!selected()) return 2;
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK), created=0, result=1;
    if(fd<0) { perror("uinput"); return 1; }
    struct uinput_setup setup={0}; setup.id.bustype=BUS_VIRTUAL;
    strcpy(setup.name,"TIC-80 Studio validation keyboard");
    if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0 || ioctl(fd,UI_SET_EVBIT,EV_SYN)<0) goto done;
    static const unsigned keys[]={KEY_F1,KEY_R,KEY_LEFTCTRL,KEY_END,KEY_SPACE,
        KEY_S,KEY_ENTER,KEY_DOWN,KEY_ESC,KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,
        KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_T,KEY_U,
        KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z,KEY_DOT};
    for(unsigned i=0;i<sizeof keys/sizeof *keys;++i) if(ioctl(fd,UI_SET_KEYBIT,keys[i])<0) goto done;
    if(ioctl(fd,UI_DEV_SETUP,&setup)<0 || ioctl(fd,UI_DEV_CREATE)<0) goto done;
    created=1;
    if(pause_ms(3000)) goto done;
    if(!strcmp(argv[1],"load-copy")) {
        if(load_copy(fd)) goto done;
    } else if(!strcmp(argv[1],"working-copy")) {
        if(chord(fd,KEY_ESC,0) || chord(fd,KEY_F1,0) ||
           chord(fd,KEY_LEFTCTRL,KEY_END) || chord(fd,KEY_SPACE,0) || chord(fd,KEY_LEFTCTRL,KEY_S)) goto done;
    } else if(!strcmp(argv[1],"edit")) {
        if(chord(fd,KEY_F1,0) || chord(fd,KEY_LEFTCTRL,KEY_END) || chord(fd,KEY_SPACE,0)) goto done;
    } else if(!strcmp(argv[1],"save")) {
        if(chord(fd,KEY_LEFTCTRL,KEY_S)) goto done;
    } else if(!strcmp(argv[1],"cancel")) {
        if(chord(fd,KEY_ENTER,0)) goto done;
    } else if(!strcmp(argv[1],"yes")) {
        if(chord(fd,KEY_DOWN,0) || chord(fd,KEY_ENTER,0)) goto done;
    } else if(!strcmp(argv[1],"run")) {
        if(chord(fd,KEY_LEFTCTRL,KEY_R)) goto done;
    } else if(chord(fd,KEY_ESC,0)) goto done;
    if(pause_ms(300)) goto done;
    result=0; printf("Studio synthetic keyboard: %s delivered and released\n",argv[1]);
done:
    if(created) ioctl(fd,UI_DEV_DESTROY);
    close(fd); return result;
}
