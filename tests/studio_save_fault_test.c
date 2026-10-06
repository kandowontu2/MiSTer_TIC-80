#define _GNU_SOURCE
#include "tic80_mister/studio_session.h"
#include "tic80_mister/pmem.h"
#include "studio/studio.h"
#include "cart.h"
#include <ftw.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int blocked,entered,released,fail_write;
static char save_prefix[512],reject_open[512];
static u64 frames;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
FILE *__real_fopen(const char *,const char *);
FILE *__real_fopen64(const char *,const char *);
FILE *__wrap_fopen(const char *path,const char *mode)
{ if(*reject_open && !strcmp(path,reject_open)) { errno=EIO; return NULL; } return __real_fopen(path,mode); }
FILE *__wrap_fopen64(const char *path,const char *mode)
{ if(*reject_open && !strcmp(path,reject_open)) { errno=EIO; return NULL; } return __real_fopen64(path,mode); }
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
    char descriptor[64],path[1024]; snprintf(descriptor,sizeof descriptor,"/proc/self/fd/%d",fd);
    ssize_t size=readlink(descriptor,path,sizeof path-1);
    if(size>=0) {
        path[size]=0;
        if(!strncmp(path,save_prefix,strlen(save_prefix)) && strstr(path,".pmem.tmp-")) {
            pthread_mutex_lock(&lock);
            if(blocked) { entered=1; pthread_cond_broadcast(&changed); while(!released) pthread_cond_wait(&changed,&lock); }
            int fail=fail_write;
            if(fail) { entered=1; pthread_cond_broadcast(&changed); }
            pthread_mutex_unlock(&lock);
            if(fail) { errno=EIO; return -1; }
        }
    }
    return __real_fsync(fd);
}
static void *release_disk(void *unused)
{
    (void)unused; usleep(500000);
    pthread_mutex_lock(&lock); released=1; blocked=0; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
    return NULL;
}
static u8 *cart(const char *id,unsigned increment,s32 *size,char key[33])
{
    tic_cartridge *source=calloc(1,sizeof *source); CHECK(source);
    snprintf(source->code.data,sizeof source->code.data,"-- saveid: %s\nfunction TIC() pmem(0,pmem(0)+%u) cls(%u) end\n",id,increment,increment&15);
    u8 *bytes=malloc(sizeof *source*2); CHECK(bytes); *size=tic_cart_save(source,bytes); free(source); CHECK(*size>0);
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(tic);
    tic80_load(tic,bytes,*size); tm_pmem_key(tic,key); tic80_delete(tic); return bytes;
}
static int remove_entry(const char *path,const struct stat *info,int kind,struct FTW *walk)
{ (void)info; (void)kind; (void)walk; return remove(path); }
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    CHECK(argc==1); char root[]="/tmp/tic80-studio-slow-save-XXXXXX"; CHECK(mkdtemp(root));
    char folder[256],saves[256],path[512],key_a[33],key_b[33];
    snprintf(folder,sizeof folder,"%s/studio",root); snprintf(saves,sizeof saves,"%s/saves",root);
    CHECK(mkdir(folder,0700)==0 && mkdir(saves,0700)==0);
    s32 size_a,size_b; u8 *a=cart("slow-a",1,&size_a,key_a),*b=cart("slow-b",200,&size_b,key_b);
    snprintf(save_prefix,sizeof save_prefix,"%s/%s",saves,key_a);
    tm_studio_session *s=NULL; CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    for(unsigned i=0;i<120;++i) CHECK(tm_studio_session_tick(s,(tic80_input){0},frames++*1000000000ULL/60,1000)==TM_STUDIO_OK);
    blocked=1;
    CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    pthread_mutex_lock(&lock); while(!entered) pthread_cond_wait(&changed,&lock); pthread_mutex_unlock(&lock);
    CHECK(tm_studio_session_load(s,b,size_b,"b.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==1);
    tic80_input run={0}; run.keyboard.keys[0]=tic_key_ctrl; run.keyboard.keys[1]=tic_key_r;
    CHECK(tm_studio_session_begin_tick(s,run,frames++*1000000000ULL/60,200)==TM_STUDIO_OK);
    pthread_t release; CHECK(pthread_create(&release,NULL,release_disk,NULL)==0);
    double began=now(),maximum=0; unsigned pending=0; int result;
    do {
        double start=now(); result=tm_studio_session_poll(s,0); double duration=now()-start;
        if(duration>maximum) maximum=duration;
        CHECK(duration<.1);
        if(result==TM_STUDIO_PENDING) {
            ++pending; CHECK(tm_studio_session_persistent(s)[0]==1);
            CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE); usleep(1000);
        }
        CHECK(now()-began<3);
    } while(result==TM_STUDIO_PENDING);
    CHECK(pthread_join(release,NULL)==0);
    CHECK(result==TM_STUDIO_OK && pending>10 && now()-began>.45);
    CHECK(tm_studio_session_persistent(s)[0]==200);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    u32 values[256]={0}; tm_pmem save={0};
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_a); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==1); tm_pmem_close(&save);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_b); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==200); tm_pmem_close(&save);
    // Pause while a completed worker ACK is still waiting for identity I/O.
    // The candidate B tick would produce 400; its prepared disk baseline is
    // 200. Never flush A's acknowledged values into B or save the unaccepted B.
    pthread_mutex_lock(&lock); blocked=1; entered=released=0; pthread_mutex_unlock(&lock);
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    for(unsigned i=0;i<120;++i) CHECK(tm_studio_session_tick(s,(tic80_input){0},frames++*1000000000ULL/60,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    pthread_mutex_lock(&lock); while(!entered) pthread_cond_wait(&changed,&lock); pthread_mutex_unlock(&lock);
    CHECK(tm_studio_session_load(s,b,size_b,"b.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_begin_tick(s,run,frames++*1000000000ULL/60,200)==TM_STUDIO_OK);
    double waiting=now();
    do { CHECK(tm_studio_session_poll(s,0)==TM_STUDIO_PENDING); CHECK(tm_studio_session_persistent(s)[0]==2); usleep(1000); } while(now()-waiting<.1);
    double pause_started=now(); CHECK(tm_studio_session_begin_pause(s)==TM_STUDIO_OK); CHECK(now()-pause_started<.1);
    CHECK(pthread_create(&release,NULL,release_disk,NULL)==0);
    waiting=now();
    do {
        double start=now(); result=tm_studio_session_poll(s,0); CHECK(now()-start<.1);
        CHECK(tm_studio_session_persistent(s)[0]==2);
        CHECK(now()-waiting<3); if(result==TM_STUDIO_PENDING) usleep(1000);
    } while(result==TM_STUDIO_PENDING);
    CHECK(result==TM_STUDIO_RECOVERED && tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK); CHECK(pthread_join(release,NULL)==0);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_a); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==2); tm_pmem_close(&save);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_b); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==200); tm_pmem_close(&save);
    // A failed old-identity flush rejects the new ACK, preserves the last
    // accepted view and leaves both previously valid disk saves unchanged.
    pthread_mutex_lock(&lock); fail_write=1; entered=0; pthread_mutex_unlock(&lock);
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    for(unsigned i=0;i<120;++i) CHECK(tm_studio_session_tick(s,(tic80_input){0},frames++*1000000000ULL/60,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    pthread_mutex_lock(&lock); while(!entered) pthread_cond_wait(&changed,&lock); pthread_mutex_unlock(&lock);
    CHECK(tm_studio_session_load(s,b,size_b,"b.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_begin_tick(s,run,frames++*1000000000ULL/60,200)==TM_STUDIO_OK);
    waiting=now();
    do { result=tm_studio_session_poll(s,0); CHECK(now()-waiting<2); if(result==TM_STUDIO_PENDING) usleep(1000); } while(result==TM_STUDIO_PENDING);
    CHECK(result==TM_STUDIO_RECOVERED && tm_studio_session_save_error(s));
    CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
    CHECK(tm_studio_session_persistent(s)[0]==3);
    for(unsigned i=0;i<25;++i) {
        CHECK(tm_studio_session_tick(s,(tic80_input){0},frames++*1000000000ULL/60,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_save_error(s));
    }
    // The unrolled warning has a full-width solid first row and text below
    // it. Refreshing the animation every tick leaves that row invisible.
    const u32 *screen=tm_studio_session_screen(s);
    u32 banner=screen[TIC80_MARGIN_TOP*TIC80_FULLWIDTH+TIC80_MARGIN_LEFT];
    CHECK(banner!=screen[(TIC80_MARGIN_TOP+20)*TIC80_FULLWIDTH+TIC80_MARGIN_LEFT]);
    for(unsigned x=0;x<TIC80_WIDTH;++x)
        CHECK(screen[TIC80_MARGIN_TOP*TIC80_FULLWIDTH+TIC80_MARGIN_LEFT+x]==banner);
    const char *capture=getenv("TM_TEST_SAVE_SCREEN");
    if(capture) {
        png_img image={.width=TIC80_FULLWIDTH,.height=TIC80_FULLHEIGHT,.values=(u32*)tm_studio_session_screen(s)};
        png_buffer png=png_write(image,(png_buffer){0}); CHECK(png.data && png.size>0);
        FILE *out=fopen(capture,"wb"); CHECK(out && fwrite(png.data,1,png.size,out)==(size_t)png.size && fclose(out)==0); free(png.data);
    }
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_a); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==2); tm_pmem_close(&save);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_b); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==200); tm_pmem_close(&save);
    pthread_mutex_lock(&lock); fail_write=0; pthread_mutex_unlock(&lock);
    // Keep this session alive and retry the selected B cartridge after storage
    // recovers. A's unsaved ACK=3 must be flushed before B's first accepted 400.
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(!tm_studio_session_save_error(s) && tm_studio_session_persistent(s)[0]==400);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_a); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==3); tm_pmem_close(&save);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_b); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==400); tm_pmem_close(&save);
    // Parent-only rejection after the worker has read B: the old context must
    // remain available for the restored ACK and the following RUN retry.
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_load(s,b,size_b,"b.tic")==TM_STUDIO_OK);
    snprintf(reject_open,sizeof reject_open,"%s/%s.pmem",saves,key_b);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_save_error(s) && tm_studio_session_persistent(s)[0]==4);
    CHECK(tm_studio_session_tick(s,(tic80_input){0},frames++*1000000000ULL/60,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_save_error(s));
    reject_open[0]=0;
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(!tm_studio_session_save_error(s) && tm_studio_session_persistent(s)[0]==600);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_a); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==4); tm_pmem_close(&save);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_b); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==600); tm_pmem_close(&save);
    // Permanent failure can keep the editor alive, but shutdown must still
    // report that its final acknowledged persistent values are not durable.
    pthread_mutex_lock(&lock); fail_write=1; entered=0; pthread_mutex_unlock(&lock);
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    pthread_mutex_lock(&lock); while(!entered) pthread_cond_wait(&changed,&lock); pthread_mutex_unlock(&lock);
    CHECK(tm_studio_session_load(s,b,size_b,"b.tic")==TM_STUDIO_OK);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_persistent(s)[0]==5);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_ERROR);
    pthread_mutex_lock(&lock); fail_write=0; pthread_mutex_unlock(&lock);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_a); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==4); tm_pmem_close(&save);
    snprintf(path,sizeof path,"%s/%s.pmem",saves,key_b); CHECK(tm_pmem_open_values(&save,values,path)==0);
    CHECK(values[0]==600); tm_pmem_close(&save);
    free(a); free(b); CHECK(nftw(root,remove_entry,32,FTW_DEPTH|FTW_PHYS)==0);
    printf("Studio slow save: 500 ms blocked fsync, %u pending polls, max poll %.3f ms; execution deadline, private ACK, identity cancellation, failed flush/open recovery, in-session retry and permanent failure close passed\n",pending,maximum*1000);
    return 0;
}
