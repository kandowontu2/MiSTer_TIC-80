#define _GNU_SOURCE
#include "tic80_mister/studio_session.h"
#include "tic80_mister/pmem.h"
#include "studio/studio.h"
#include "cart.h"
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static bool worker;
static FILE *read_file;
static char fault[16];
static u64 frame;
FILE *__real_fopen(const char *,const char *);
FILE *__real_fopen64(const char *,const char *);
size_t __real_fread(void *,size_t,size_t,FILE *);
int __real_fclose(FILE *);
static bool injected(const char *path)
{
    const char *target=getenv("TM_TEST_READ_PATH"),*control=getenv("TM_TEST_READ_CONTROL");
    if(!worker || !target || strcmp(path,target) || !control) return false;
    FILE *file=__real_fopen(control,"rb"); CHECK(file);
    memset(fault,0,sizeof fault); CHECK(__real_fread(fault,1,sizeof fault-1,file)>0);
    CHECK(__real_fclose(file)==0);
    if(!strcmp(fault,"deny")) { errno=EACCES; return true; }
    if(!strcmp(fault,"io")) { errno=EIO; return true; }
    if(!strcmp(fault,"delay")) usleep(500000);
    return false;
}
FILE *__wrap_fopen(const char *path,const char *mode)
{
    if(injected(path)) return NULL;
    FILE *file=__real_fopen(path,mode);
    if(worker && getenv("TM_TEST_READ_PATH") && !strcmp(path,getenv("TM_TEST_READ_PATH"))) read_file=file;
    return file;
}
FILE *__wrap_fopen64(const char *path,const char *mode)
{
    if(injected(path)) return NULL;
    FILE *file=__real_fopen64(path,mode);
    if(worker && getenv("TM_TEST_READ_PATH") && !strcmp(path,getenv("TM_TEST_READ_PATH"))) read_file=file;
    return file;
}
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *file)
{
    if(worker && file==read_file && !strcmp(fault,"short")) count/=2;
    return __real_fread(out,size,count,file);
}
int __wrap_fclose(FILE *file)
{
    bool reject=worker && file==read_file && !strcmp(fault,"close");
    if(file==read_file) read_file=NULL;
    int result=__real_fclose(file);
    if(reject) { errno=EIO; return EOF; }
    return result;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static void control(const char *path,const char *mode)
{ FILE *file=fopen(path,"wb"); CHECK(file && fputs(mode,file)>=0 && fclose(file)==0); }
static u8 *cart(const char *id,s32 *size,char key[33])
{
    tic_cartridge *source=calloc(1,sizeof *source); CHECK(source);
    snprintf(source->code.data,sizeof source->code.data,
        "-- saveid: %s\nfunction BOOT() pmem(1,pmem(1)+1) mset(0,0,77) sync(4,0,true) end "
        "function TIC() pmem(0,pmem(0)+1) pmem(255,0x89abcdef) cls(6) end\n",id);
    u8 *bytes=malloc(sizeof *source*2); CHECK(bytes); *size=tic_cart_save(source,bytes); free(source); CHECK(*size>0);
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(tic);
    tic80_load(tic,bytes,*size); tm_pmem_key(tic,key); tic80_delete(tic); return bytes;
}
static void seed(const char *path,u32 count,u32 boots)
{
    u32 values[256]={0}; tm_pmem save={0}; CHECK(tm_pmem_open_values(&save,values,path)==0);
    values[0]=count; values[1]=boots; values[255]=0x89abcdef;
    CHECK(tm_pmem_save_values(&save,values)==0); tm_pmem_close(&save);
}
static void disk_values(const char *path,u32 out[256])
{ tm_pmem save={0}; CHECK(tm_pmem_open_values(&save,out,path)==0); tm_pmem_close(&save); }
static int remove_entry(const char *path,const struct stat *info,int kind,struct FTW *walk)
{ (void)info; (void)kind; (void)walk; return remove(path); }
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) { worker=true; return tm_studio_session_worker(argc,argv); }
    CHECK(argc==1); char root[]="/tmp/tic80-studio-read-XXXXXX"; CHECK(mkdtemp(root));
    char folder[256],saves[256],switch_file[256],path_a[512],path_b[512],key_a[33],key_b[33];
    snprintf(folder,sizeof folder,"%s/studio",root); snprintf(saves,sizeof saves,"%s/saves",root);
    snprintf(switch_file,sizeof switch_file,"%s/read-control",root);
    CHECK(mkdir(folder,0700)==0 && mkdir(saves,0700)==0);
    s32 size_a,size_b; u8 *a=cart("read-a",&size_a,key_a),*b=cart("read-b",&size_b,key_b);
    snprintf(path_a,sizeof path_a,"%s/%s.pmem",saves,key_a); snprintf(path_b,sizeof path_b,"%s/%s.pmem",saves,key_b);
    CHECK(setenv("TM_TEST_READ_PATH",path_b,1)==0 && setenv("TM_TEST_READ_CONTROL",switch_file,1)==0);
    seed(path_a,10,2);
    const char *modes[]={"deny","io","short","close","delay"};
    double maximum=0;
    for(unsigned i=0;i<sizeof modes/sizeof *modes;++i) {
        seed(path_b,100,9); control(switch_file,modes[i]);
        tm_studio_session *s=NULL; CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
        for(unsigned n=0;n<120;++n) CHECK(tm_studio_session_tick(s,(tic80_input){0},frame++*1000000000ULL/60,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK && tm_studio_session_run(s,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_load(s,b,size_b,"b.tic")==TM_STUDIO_OK);
        tic_cartridge *checkpoint=malloc(sizeof *checkpoint); CHECK(checkpoint); *checkpoint=*tm_studio_session_cart(s);
        u32 before[256]; memcpy(before,tm_studio_session_persistent(s),sizeof before);
        pid_t pid=tm_studio_session_pid(s);
        tic80_input run={0}; run.keyboard.keys[0]=tic_key_ctrl; run.keyboard.keys[1]=tic_key_r;
        CHECK(tm_studio_session_begin_tick(s,run,frame++*1000000000ULL/60,100)==TM_STUDIO_OK);
        double start=now(); int result;
        do {
            double poll_start=now(); result=tm_studio_session_poll(s,0); double duration=now()-poll_start;
            if(duration>maximum) maximum=duration; CHECK(duration<.1 && now()-start<2);
            if(result==TM_STUDIO_PENDING) {
                CHECK(!memcmp(before,tm_studio_session_persistent(s),sizeof before)); usleep(1000);
            }
        } while(result==TM_STUDIO_PENDING);
        bool delayed=!strcmp(modes[i],"delay");
        CHECK(result==(delayed?TM_STUDIO_RECOVERED:TM_STUDIO_SAVE_ERROR));
        CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
        CHECK(!memcmp(before,tm_studio_session_persistent(s),sizeof before));
        CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint)); /* BOOT never ran */
        CHECK(delayed?tm_studio_session_pid(s)!=pid:tm_studio_session_pid(s)==pid);
        for(unsigned n=0;n<65;++n) {
            CHECK(tm_studio_session_tick(s,(tic80_input){0},frame++*1000000000ULL/60,1000)==TM_STUDIO_OK);
            if(!delayed) CHECK(tm_studio_session_save_error(s));
        }
        if(delayed) {
            const u32 *screen=tm_studio_session_screen(s);
            u32 banner=screen[TIC80_MARGIN_TOP*TIC80_FULLWIDTH+TIC80_MARGIN_LEFT];
            for(unsigned y=0;y<TIC_FONT_HEIGHT+1;++y) for(unsigned x=0;x<10;++x) {
                CHECK(screen[(TIC80_MARGIN_TOP+y)*TIC80_FULLWIDTH+TIC80_MARGIN_LEFT+x]==banner);
                CHECK(screen[(TIC80_MARGIN_TOP+y)*TIC80_FULLWIDTH+TIC80_MARGIN_LEFT+TIC80_WIDTH-1-x]==banner);
            }
            const char *capture=getenv("TM_TEST_READ_SCREEN");
            if(capture) {
                png_img image={.width=TIC80_FULLWIDTH,.height=TIC80_FULLHEIGHT,.values=(u32*)screen};
                png_buffer png=png_write(image,(png_buffer){0}); CHECK(png.data && png.size>0);
                FILE *out=fopen(capture,"wb"); CHECK(out && fwrite(png.data,1,png.size,out)==(size_t)png.size && fclose(out)==0); free(png.data);
            }
        }
        u32 disk[256]; disk_values(path_b,disk); CHECK(disk[0]==100 && disk[1]==9 && disk[255]==0x89abcdef);
        control(switch_file,"none");
        CHECK(tm_studio_session_begin_run(s,1000)==TM_STUDIO_OK);
        do { result=tm_studio_session_poll(s,1); } while(result==TM_STUDIO_PENDING);
        CHECK(result==TM_STUDIO_OK && !tm_studio_session_save_error(s));
        CHECK(tm_studio_session_persistent(s)[0]==101 && tm_studio_session_persistent(s)[1]==10);
        CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
        disk_values(path_a,disk); CHECK(!memcmp(before,disk,sizeof before));
        disk_values(path_b,disk); CHECK(disk[0]==101 && disk[1]==10 && disk[255]==0x89abcdef);
        free(checkpoint); printf("Studio save read %s: rejected before BOOT/TIC, editor alive, disk preserved, retry passed\n",modes[i]);
    }
    CHECK(unsetenv("TM_TEST_READ_PATH")==0 && unsetenv("TM_TEST_READ_CONTROL")==0);
    free(a); free(b); CHECK(nftw(root,remove_entry,32,FTW_DEPTH|FTW_PHYS)==0);
    printf("Studio save reads: five worker read faults, asynchronous timeout/recovery and retries passed; max poll %.3f ms\n",maximum*1000);
    return 0;
}
