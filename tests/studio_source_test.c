#define _GNU_SOURCE
#include "tic80_mister/studio_session.h"
#include "cart.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s errno=%d\n",__LINE__,#c,errno); exit(1); } } while(0)
static u64 frames;
static int tick(tm_studio_session *s,tic80_input input)
{ return tm_studio_session_tick(s,input,frames++*1000000000ULL/60,1000); }
static void idle(tm_studio_session *s,unsigned count)
{ while(count--) CHECK(tick(s,(tic80_input){0})==TM_STUDIO_OK); }
static void key(tm_studio_session *s,tic_key key,tic_key modifier)
{ tic80_input input={0}; input.keyboard.keys[0]=key; input.keyboard.keys[1]=modifier; CHECK(tick(s,input)==TM_STUDIO_OK); idle(s,1); }
static int finish(tm_studio_session *s)
{ int result; do { result=tm_studio_session_poll(s,10); } while(result==TM_STUDIO_PENDING); return result; }
static void edit(tm_studio_session *s)
{ key(s,tic_key_f1,0); key(s,tic_key_end,tic_key_ctrl); CHECK(tm_studio_session_clipboard(s,"-- source ownership edit\n")==TM_STUDIO_OK); key(s,tic_key_v,tic_key_ctrl); CHECK(tm_studio_session_modified(s)); }
static int answer(tm_studio_session *s,int yes)
{
    idle(s,12); tic80_input input={0};
    if(yes) { input.gamepads.data=1u<<1; CHECK(tick(s,input)==TM_STUDIO_OK); idle(s,12); }
    input.gamepads.data=1u<<4; CHECK(tick(s,input)==TM_STUDIO_OK);
    int result=TM_STUDIO_OK; for(unsigned i=0;i<20 && result==TM_STUDIO_OK;++i) result=tick(s,(tic80_input){0}); return result;
}
static void write_file(const char *path,const u8 *data,s32 size)
{ FILE *file=fopen(path,"wb"); CHECK(file && fwrite(data,1,size,file)==(size_t)size && !fclose(file)); }
static u8 *cart(const char *code,s32 *size)
{ tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); strcpy(cart->code.data,code); u8 *data=malloc(2*sizeof *cart); CHECK(data); *size=tic_cart_save(cart,data); free(cart); return data; }
static void check_code(const char *path,const char *expected)
{
    FILE *file=fopen(path,"rb"); CHECK(file); CHECK(!fseek(file,0,SEEK_END)); long size=ftell(file); rewind(file);
    u8 *bytes=malloc(size); CHECK(bytes && fread(bytes,1,size,file)==(size_t)size && !fclose(file));
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); tic_cart_load(cart,bytes,size);
    CHECK(!strcmp(cart->code.data,expected)); free(cart); free(bytes);
}
static int remove_entry(const char *path,const struct stat *info,int kind,struct FTW *walk)
{ (void)info; (void)kind; (void)walk; return remove(path); }
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    CHECK(argc==1); char folder[]="/tmp/tic80-studio-source-XXXXXX"; CHECK(mkdtemp(folder));
    char a[256],b[256],working[256],directory[256],source[256];
    snprintf(directory,sizeof directory,"%s/a",folder); CHECK(!mkdir(directory,0700));
    snprintf(directory,sizeof directory,"%s/b",folder); CHECK(!mkdir(directory,0700));
    snprintf(a,sizeof a,"%s/a/cart.tic",folder); snprintf(b,sizeof b,"%s/b/cart.tic",folder);
    snprintf(working,sizeof working,"%s/working.tic",folder);
    const char *code="function TIC() cls(6) end\n",*different="function TIC() cls(9) end\n";
    s32 size,other_size; u8 *bytes=cart(code,&size),*other=cart(different,&other_size); CHECK(size==other_size);
    write_file(a,bytes,size); write_file(b,bytes,size);
    tm_studio_session *s=NULL; CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
    CHECK(tm_studio_session_load_file(s,a,1000)==TM_STUDIO_OK); idle(s,120);
    // Identical bytes and identical basenames in distinct folders must bind
    // to the path sent with this transfer, never a previous/logged alias.
    strcpy(source,b); u8 *copy=malloc(size); CHECK(copy); memcpy(copy,bytes,size);
    CHECK(tm_studio_session_begin_select_source(s,copy,size,"working.tic",source,1000)==TM_STUDIO_OK);
    memset(source,'x',sizeof source); memset(copy,'x',size); free(copy);
    CHECK(finish(s)==TM_STUDIO_CART_SELECTED && !strcmp(tm_studio_session_name(s)->path,b));
    CHECK(!strcmp(tm_studio_session_name(s)->name,"cart.tic"));
    edit(s); key(s,tic_key_s,tic_key_ctrl); CHECK(!tm_studio_session_modified(s));
    check_code(a,code); check_code(b,tm_studio_session_cart(s)->code.data);
    write_file(b,bytes,size); CHECK(tm_studio_session_load_file(s,a,1000)==TM_STUDIO_OK); edit(s);
    CHECK(tm_studio_session_begin_select_source(s,bytes,size,"working.tic",b,1000)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_CONFIRM);
    CHECK(answer(s,0)==TM_STUDIO_CANCELLED && !strcmp(tm_studio_session_name(s)->path,a) && tm_studio_session_modified(s));
    CHECK(tm_studio_session_begin_select_source(s,bytes,size,"working.tic",b,1000)==TM_STUDIO_OK); CHECK(finish(s)==TM_STUDIO_CONFIRM);
    write_file(b,other,other_size); // changed while awaiting approval
    CHECK(answer(s,1)==TM_STUDIO_CART_SELECTED);
    CHECK(!strcmp(tm_studio_session_name(s)->name,"working.tic") && !*tm_studio_session_name(s)->path);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    edit(s); key(s,tic_key_s,tic_key_ctrl); CHECK(!tm_studio_session_modified(s));
    check_code(b,different); check_code(working,tm_studio_session_cart(s)->code.data);
    char *saved_code=strdup(tm_studio_session_cart(s)->code.data); CHECK(saved_code);
    // Missing/virtual and nonregular sources retain a working destination.
    snprintf(source,sizeof source,"%s/archive.zip/cart.tic",folder);
    CHECK(tm_studio_session_begin_select_source(s,bytes,size,"working.tic",source,1000)==TM_STUDIO_OK);
    CHECK(finish(s)==TM_STUDIO_CART_SELECTED && !*tm_studio_session_name(s)->path);
    snprintf(source,sizeof source,"%s/pipe.tic",folder); CHECK(!mkfifo(source,0600));
    CHECK(tm_studio_session_begin_select_source(s,bytes,size,"working.tic",source,1000)==TM_STUDIO_OK);
    CHECK(finish(s)==TM_STUDIO_CART_SELECTED && !*tm_studio_session_name(s)->path);
    // A later anonymous cart must create a separate copy, retaining an
    // earlier working copy and a dangling alias occupying its next name.
    char second[256],occupied[256],third[256];
    snprintf(second,sizeof second,"%s/working-2.tic",folder);
    snprintf(occupied,sizeof occupied,"%s/not-there",folder); CHECK(!symlink(occupied,second));
    snprintf(third,sizeof third,"%s/working-3.tic",folder);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL));
    CHECK(tick(s,(tic80_input){0})==TM_STUDIO_RECOVERED && !*tm_studio_session_name(s)->path);
    check_code(working,saved_code);
    edit(s); key(s,tic_key_s,tic_key_ctrl); CHECK(!tm_studio_session_modified(s));
    CHECK(!strcmp(tm_studio_session_name(s)->name,"working-3.tic") && !strcmp(tm_studio_session_name(s)->path,third));
    check_code(working,saved_code); check_code(third,tm_studio_session_cart(s)->code.data); free(saved_code);
    edit(s); key(s,tic_key_s,tic_key_ctrl); CHECK(!tm_studio_session_modified(s));
    CHECK(!strcmp(tm_studio_session_name(s)->path,third));
    check_code(third,tm_studio_session_cart(s)->code.data);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK); free(bytes); free(other);
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD); CHECK(!nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS));
    puts("Studio source: transfer-owned path/payload copies, identical aliases, original-path Save, default-NO preservation, approval-time byte verification and mismatching/missing/FIFO fallback passed; distinct working copies, occupied aliases, recovery and subsequent Save preserve earlier files"); return 0;
}
