#define _XOPEN_SOURCE 700
#include "tic80_mister/studio_session.h"
#include "studio/studio.h"
#include "cart.h"
#include <errno.h>
#include <ftw.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static u64 frames;
static int tick(tm_studio_session *s,tic80_input input)
{ return tm_studio_session_tick(s,input,frames++*1000000000ULL/60,1000); }
static void idle(tm_studio_session *s,unsigned count)
{ while(count--) CHECK(tick(s,(tic80_input){0})==TM_STUDIO_OK); }
static void key(tm_studio_session *s,tic_key code,tic_key modifier)
{ tic80_input input={0}; input.keyboard.keys[0]=code; input.keyboard.keys[1]=modifier; CHECK(tick(s,input)==TM_STUDIO_OK); idle(s,1); }
static int finish(tm_studio_session *s)
{ int result; do { result=tm_studio_session_poll(s,10); } while(result==TM_STUDIO_PENDING); return result; }
static u8 *cart(const char *code,s32 *size)
{
    tic_cartridge *source=calloc(1,sizeof *source); CHECK(source); strcpy(source->code.data,code);
    u8 *data=malloc(2*sizeof *source); CHECK(data); *size=tic_cart_save(source,data); free(source); CHECK(*size>0); return data;
}
static int answer(tm_studio_session *s,int yes)
{
    idle(s,12);
    tic80_input input={0};
    if(yes) { input.gamepads.data=1u<<1; CHECK(tick(s,input)==TM_STUDIO_OK); idle(s,12); }
    input.gamepads.data=1u<<4; CHECK(tick(s,input)==TM_STUDIO_OK);
    int result=TM_STUDIO_OK;
    for(unsigned i=0;i<20 && result==TM_STUDIO_OK;++i) result=tick(s,(tic80_input){0});
    return result;
}
static int remove_entry(const char *path,const struct stat *info,int kind,struct FTW *walk)
{ (void)info; (void)kind; (void)walk; return remove(path); }
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    CHECK(argc==1); char folder[]="/tmp/tic80-studio-selection-XXXXXX"; CHECK(mkdtemp(folder));
    tm_studio_session *s=NULL; CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
    s32 size_a,size_b; u8 *a=cart("function TIC() cls(6) end\n",&size_a),*b=cart("function TIC() cls(12) end\n",&size_b);
    CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK); idle(s,120);
    CHECK(!tm_studio_session_modified(s));
    CHECK(tm_studio_session_begin_select(s,b,size_b,"b.tic",1000)==TM_STUDIO_OK);
    CHECK(finish(s)==TM_STUDIO_CART_SELECTED && !tm_studio_session_selecting(s));
    CHECK(!strcmp(tm_studio_session_name(s)->name,"b.tic"));
    CHECK(tm_studio_session_load(s,a,size_a,"a.tic")==TM_STUDIO_OK);
    key(s,tic_key_f1,0); key(s,tic_key_end,tic_key_ctrl);
    CHECK(tm_studio_session_clipboard(s,"-- unsaved selection edit\n")==TM_STUDIO_OK);
    key(s,tic_key_v,tic_key_ctrl); CHECK(tm_studio_session_modified(s));
    tic_cartridge *checkpoint=malloc(sizeof *checkpoint); CHECK(checkpoint); *checkpoint=*tm_studio_session_cart(s);
    CHECK(tm_studio_session_begin_select(s,(u8*)"junk",4,"bad.tic",1000)==TM_STUDIO_OK);
    CHECK(finish(s)==TM_STUDIO_CART_ERROR && !tm_studio_session_selecting(s));
    CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_begin_select(s,b,size_b,"b.tic",1000)==TM_STUDIO_OK);
    CHECK(finish(s)==TM_STUDIO_CONFIRM && tm_studio_session_selecting(s));
    CHECK(tm_studio_session_begin_run(s,1000)==TM_STUDIO_ERROR);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_ERROR);
    CHECK(tm_studio_session_begin_load(s,a,size_a,"overwrite.tic",1000)==TM_STUDIO_ERROR);
    CHECK(tm_studio_session_begin_select(s,a,size_a,"overwrite.tic",1000)==TM_STUDIO_ERROR);
    CHECK(answer(s,0)==TM_STUDIO_CANCELLED && !tm_studio_session_selecting(s));
    CHECK(tm_studio_session_mode(s)==TIC_CODE_MODE && tm_studio_session_modified(s));
    CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_begin_select(s,b,size_b,"b.tic",1000)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_CONFIRM);
    // Recovery cancels the old callback, retaining edits. It never implicitly
    // approves a candidate, even if the last ACK contains a visible dialog.
    CHECK(tm_studio_session_begin_pause(s)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_RECOVERED);
    CHECK(!tm_studio_session_selecting(s) && !memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_begin_select(s,b,size_b,"b.tic",1000)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_CONFIRM);
    pid_t prior=tm_studio_session_pid(s); CHECK(kill(prior,SIGKILL)==0);
    CHECK(tick(s,(tic80_input){0})==TM_STUDIO_RECOVERED);
    CHECK(!tm_studio_session_selecting(s) && !memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_begin_select(s,b,size_b,"b.tic",1000)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_CONFIRM);
    idle(s,12); tic80_input escape={0}; escape.keyboard.keys[0]=tic_key_escape;
    CHECK(tick(s,escape)==TM_STUDIO_CANCELLED && !tm_studio_session_selecting(s)); idle(s,1);
    CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_begin_select(s,b,size_b,"b.tic",1000)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_CONFIRM);
    CHECK(answer(s,1)==TM_STUDIO_CART_SELECTED);
    CHECK(!tm_studio_session_selecting(s) && !tm_studio_session_modified(s));
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,"function TIC() cls(12) end\n"));
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    // A running cart can sync ROM edits too. NO resumes the same VM/pmem,
    // rather than re-running BOOT or restarting the original unedited cart.
    s32 run_size; u8 *running=cart("function BOOT() pmem(1,pmem(1)+1) end function TIC() pmem(0,pmem(0)+1) mset(0,0,77) sync(4,7,true) end\n",&run_size);
    CHECK(tm_studio_session_load(s,running,run_size,"running.tic")==TM_STUDIO_OK); free(running);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK); idle(s,3);
    CHECK(tm_studio_session_modified(s) && tm_studio_session_cart(s)->banks[7].map.data[0]==77);
    u32 persistent=tm_studio_session_persistent(s)[0],boots=tm_studio_session_persistent(s)[1]; prior=tm_studio_session_pid(s);
    CHECK(tm_studio_session_begin_select(s,b,size_b,"b.tic",1000)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_CONFIRM);
    idle(s,12); CHECK(tm_studio_session_persistent(s)[0]==persistent);
    CHECK(answer(s,0)==TM_STUDIO_CANCELLED && tm_studio_session_mode(s)==TIC_RUN_MODE);
    CHECK(tm_studio_session_persistent(s)[0]==persistent && tm_studio_session_persistent(s)[1]==boots);
    CHECK(tm_studio_session_pid(s)==prior && tm_studio_session_cart(s)->banks[7].map.data[0]==77);
    CHECK(tick(s,(tic80_input){0})==TM_STUDIO_OK && tm_studio_session_persistent(s)[0]==persistent+1);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK); free(checkpoint); free(a); free(b);
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    CHECK(nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
    puts("Studio selection: clean load, malformed rejection, default-NO controller cancel, ESC cancel, one-selection ownership, reset/SIGKILL preservation, explicit YES load/RUN and dirty RUN resumption without BOOT passed");
    return 0;
}
