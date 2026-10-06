#define _XOPEN_SOURCE 700
#include "tic80_mister/studio_session.h"
#include "tic80_mister/pmem.h"
#include "studio/studio.h"
#include "cart.h"
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static u64 frame;
static int tick(tm_studio_session *s,tic80_input input)
{ return tm_studio_session_tick(s,input,frame++*1000000000ULL/60,250); }
static void idle(tm_studio_session *s,unsigned count)
{ while(count--) CHECK(tick(s,(tic80_input){0})==TM_STUDIO_OK); }
static u8 *cartridge(const char *id,s32 *size)
{
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart);
    snprintf(cart->code.data,sizeof cart->code.data,
        "-- saveid: %s\nfunction BOOT() pmem(1,pmem(1)+1) end "
        "function TIC() pmem(0,pmem(0)+1) pmem(255,0xffffffff) "
        "if btn(4) then pmem(0,9999) while true do end end end\n",id);
    cart->bank0.map.data[0]=42;
    u8 *data=malloc(sizeof *cart*2); CHECK(data);
    *size=tic_cart_save(cart,data); CHECK(*size>0); free(cart); return data;
}
static void load(tm_studio_session *s,const u8 *data,s32 size)
{
    CHECK(tm_studio_session_begin_load(s,data,size,"save.tic",1000)==TM_STUDIO_OK);
    int result; do { result=tm_studio_session_poll(s,1); } while(result==TM_STUDIO_PENDING);
    CHECK(result==TM_STUDIO_OK);
}
static int remove_entry(const char *path,const struct stat *info,int kind,struct FTW *walk)
{ (void)info; (void)kind; (void)walk; return remove(path); }
static void read_values(const char *path,u32 out[256])
{ tm_pmem save={0}; CHECK(tm_pmem_open_values(&save,out,path)==0); tm_pmem_close(&save); }
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    CHECK(argc==1);
    char root[]="/tmp/tic80-studio-save-XXXXXX"; CHECK(mkdtemp(root));
    char folder[256],saves[256],path_a[512],path_b[512];
    snprintf(folder,sizeof folder,"%s/studio",root); snprintf(saves,sizeof saves,"%s/saves",root);
    CHECK(mkdir(folder,0700)==0 && mkdir(saves,0700)==0);
    s32 size_a,size_b; u8 *a=cartridge("studio-save-a",&size_a),*b=cartridge("studio-save-b",&size_b);
    // Seed through the installed service's runtime interface and key function.
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(tic);
    tic80_load(tic,a,size_a); char key[33]; tm_pmem_key(tic,key);
    snprintf(path_a,sizeof path_a,"%s/%s.pmem",saves,key);
    tm_pmem save={0}; CHECK(tm_pmem_open(&save,tic,path_a)==0);
    ((tic_mem*)tic)->ram->persistent.data[0]=10;
    ((tic_mem*)tic)->ram->persistent.data[1]=2;
    CHECK(tm_pmem_save(&save,tic)==0); tm_pmem_close(&save);
    tic80_load(tic,b,size_b); tm_pmem_key(tic,key);
    snprintf(path_b,sizeof path_b,"%s/%s.pmem",saves,key);
    tm_studio_session *s=NULL;
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    load(s,a,size_a); CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==11 && tm_studio_session_persistent(s)[1]==3);
    idle(s,20); CHECK(tm_studio_session_persistent(s)[0]==31);
    // Same identity restarts from its ACK before the one-second autosave.
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==32 && tm_studio_session_persistent(s)[1]==4);
    idle(s,65); CHECK(tm_studio_session_persistent(s)[0]==97);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    u32 values[256]={0}; read_values(path_a,values);
    CHECK(values[0]==97 && values[1]==4 && values[255]==0xffffffff);
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    load(s,a,size_a); CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==98 && tm_studio_session_persistent(s)[1]==5);
    // Loading another cart must not save the reset RAM over the old identity.
    load(s,b,size_b); CHECK(tm_studio_session_persistent(s)[0]==98);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==1 && tm_studio_session_persistent(s)[1]==1);
    read_values(path_a,values); CHECK(values[0]==98 && values[1]==5);
    load(s,a,size_a); CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==99 && tm_studio_session_persistent(s)[1]==6);
    u32 before[256]; memcpy(before,tm_studio_session_persistent(s),sizeof before);
    tic80_input input={0}; input.gamepads.data=1u<<4;
    CHECK(tick(s,input)==TM_STUDIO_RECOVERED);
    CHECK(!memcmp(before,tm_studio_session_persistent(s),sizeof before));
    idle(s,3);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==100 && tm_studio_session_persistent(s)[1]==7);
    // A partial pending tick must also be discarded when closing the session.
    memcpy(before,tm_studio_session_persistent(s),sizeof before);
    CHECK(tm_studio_session_begin_tick(s,input,UINT64_MAX,250)==TM_STUDIO_OK);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    read_values(path_a,values); CHECK(!memcmp(before,values,sizeof before));
    // Corruption is rejected before BOOT/TIC and the corrupt bytes survive.
    FILE *file=fopen(path_b,"r+b"); CHECK(file && fputc('X',file)!=EOF && fclose(file)==0);
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    load(s,b,size_b); CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_SAVE_ERROR);
    CHECK(tm_studio_session_save_error(s)); idle(s,65); CHECK(tm_studio_session_save_error(s));
    CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    file=fopen(path_b,"rb"); CHECK(file && fgetc(file)=='X' && fclose(file)==0);
    // Without saveid metadata the service and Studio must share bank0's key.
    s32 size_c; u8 *c=cartridge("",&size_c);
    tic80_load(tic,c,size_c); tm_pmem_key(tic,key);
    char path_c[512]; snprintf(path_c,sizeof path_c,"%s/%s.pmem",saves,key);
    CHECK(tm_studio_session_open_saved(&s,folder,saves)==TM_STUDIO_OK);
    load(s,c,size_c); CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_persistent(s)[0]==1 && tm_studio_session_persistent(s)[1]==1);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    CHECK(tm_pmem_open(&save,tic,path_c)==0);
    CHECK(((tic_mem*)tic)->ram->persistent.data[0]==1);
    CHECK(((tic_mem*)tic)->ram->persistent.data[255]==0xffffffff);
    tm_pmem_close(&save); free(c);
    tic80_delete(tic); free(a); free(b);
    CHECK(nftw(root,remove_entry,32,FTW_DEPTH|FTW_PHYS)==0);
    puts("Studio shared saves: service interoperability, explicit/bank0 identities, ACK restart, switching, full u32, hang recovery, pending cancellation and corruption preservation passed");
    return 0;
}
