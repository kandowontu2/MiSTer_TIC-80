#define _XOPEN_SOURCE 700
#include "tic80_mister/studio_session.h"
#include "tic80_mister/input.h"
#include "studio/studio.h"
#include "cart.h"
#include <errno.h>
#include <dirent.h>
#include <ftw.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { int error=errno; fprintf(stderr,"failed line %d: %s; errno=%d (%s)\n",__LINE__,#c,error,strerror(error)); exit(1); } } while(0)
static uint64_t frames;
static uint64_t counter(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec; }
static int compare_time(const void *a,const void *b)
{ uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return (x>y)-(x<y); }
static int tick(tm_studio_session *s,tic80_input input,unsigned timeout)
{ return tm_studio_session_tick(s,input,frames++*1000000000ULL/60,timeout); }
static void idle(tm_studio_session *s,unsigned count)
{
    while(count--) {
        int result=tick(s,(tic80_input){0},1000);
        if(result!=TM_STUDIO_OK)
            fprintf(stderr,"idle frame=%llu result=%d mode=%d worker=%ld\n",
                (unsigned long long)frames,result,tm_studio_session_mode(s),(long)tm_studio_session_pid(s));
        CHECK(result==TM_STUDIO_OK);
    }
}
static int key(tm_studio_session *s,tic_key key,tic_key modifier,unsigned ms)
{
    tic80_input input={0}; input.keyboard.keys[0]=modifier; input.keyboard.keys[1]=key;
    int result=tick(s,input,ms); idle(s,1); return result;
}
static void load(tm_studio_session *s,const char *code)
{
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart);
    strcpy(cart->code.data,code); cart->banks[7].map.data[123]=0x9e;
    ((u8*)&cart->banks[7].sprites)[77]=0x43;
    u8 *data=malloc(sizeof *cart*2); CHECK(data);
    s32 size=tic_cart_save(cart,data); CHECK(size>0);
    CHECK(tm_studio_session_begin_load(s,data,size,"checkpoint.tic",1000)==TM_STUDIO_OK);
    free(data); data=NULL; /* the request must own its copy before returning */
    int loaded; do { loaded=tm_studio_session_poll(s,1); } while(loaded==TM_STUDIO_PENDING);
    CHECK(loaded==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_name(s)->name,"checkpoint.tic"));
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    free(cart);
}
static void mouse_click(tm_studio_session *s,unsigned x,unsigned y)
{
    CHECK(x<240 && y<136);
    tm_input_snapshot snapshot={.mouse=x|(y<<8)};
    tm_input_state state={0};
    // Toolbar tab requests apply on the frame after button release.
    for(unsigned phase=0;phase<4;++phase) {
        snapshot.mouse=x|(y<<8)|(phase==1?1u<<16:0);
        tic80_input input={0}; tm_input_convert(&state,&snapshot,&input);
        CHECK(tick(s,input,1000)==TM_STUDIO_OK);
    }
}
static unsigned sprite_pixel(tm_studio_session *s,unsigned bank)
{ return tm_studio_session_cart(s)->banks[bank].tiles.data[1].data[0]&15; }
static const void *asset_region(const tic_cartridge *cart,unsigned bank,unsigned kind,size_t *size)
{
    const tic_bank *data=cart->banks+bank;
    switch(kind) {
    case 0: *size=TIC_SPRITES*sizeof(tic_tile); return &data->tiles;
    case 1: *size=sizeof data->map; return &data->map;
    case 2: *size=sizeof data->sfx.samples; return &data->sfx.samples;
    case 3: *size=sizeof data->sfx.waveforms; return &data->sfx.waveforms;
    case 4: *size=sizeof data->music; return &data->music;
    default: CHECK(false); return NULL;
    }
}
static void paint(tm_studio_session *s,unsigned x,unsigned y)
{
    tm_input_state state={0};
    for(unsigned phase=0;phase<5;++phase) {
        tm_input_snapshot snapshot={.mouse=x|(y<<8)|((phase==1 || phase==2)?1u<<16:0)};
        tic80_input input={0}; tm_input_convert(&state,&snapshot,&input);
        CHECK(tick(s,input,1000)==TM_STUDIO_OK);
    }
}
static void asset_bank(tm_studio_session *s,unsigned bank,unsigned kind)
{
    const unsigned tab[]={1,2,3,3,4}; const EditorMode mode[]={TIC_SPRITE_MODE,TIC_MAP_MODE,TIC_SFX_MODE,TIC_SFX_MODE,TIC_MUSIC_MODE};
    mouse_click(s,tab[kind]*TOOLBAR_SIZE,0); CHECK(tm_studio_session_mode(s)==mode[kind]);
    mouse_click(s,5*TOOLBAR_SIZE+2+2+(bank+1)*TOOLBAR_SIZE+1,1);
}
static void asset_edit(tm_studio_session *s,unsigned kind,unsigned step)
{
    switch(kind) {
    case 0: mouse_click(s,24+(5+step)*8+4,116); paint(s,28,24); break;
    case 1:
        CHECK(key(s,tic_key_shift,0,1000)==TM_STUDIO_OK); idle(s,30);
        paint(s,111+(1+step)*8+4,TOOLBAR_SIZE+4); idle(s,30);
        paint(s,4,TOOLBAR_SIZE+4); break;
    case 2: paint(s,72+step*2,15); break;
    case 3: paint(s,13,67-step*2); break;
    case 4: mouse_click(s,109,TOOLBAR_SIZE+4); break;
    default: CHECK(false);
    }
}
static void asset_undo(tm_studio_session *s,unsigned kind,bool redo)
{
    if(kind==3) mouse_click(s,74,87+(redo?4:3)*7);
    else CHECK(key(s,redo?tic_key_y:tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
}
static void asset_expect(tm_studio_session *s,tic_cartridge *expected,unsigned bank,unsigned kind,const void *bytes,size_t length)
{
    size_t size; void *region=(void*)asset_region(expected,bank,kind,&size); CHECK(size==length);
    memcpy(region,bytes,length);
    CHECK(!memcmp(tm_studio_session_cart(s),expected,sizeof *expected));
}
static void asset_histories(tm_studio_session *s)
{
    load(s,"function TIC() while true do end end\n"); idle(s,120);
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    tic_cartridge *original=malloc(sizeof *original),*expected=malloc(sizeof *expected); CHECK(original&&expected);
    *original=*expected=*tm_studio_session_cart(s);
    u8 *first[8][5]={0},*second[8][5]={0}; size_t lengths[5]={0};
    mouse_click(s,TOOLBAR_SIZE,0); mouse_click(s,5*TOOLBAR_SIZE+3,1); // open bank selector
    for(unsigned bank=0;bank<8;++bank) for(unsigned kind=0;kind<5;++kind) {
        asset_bank(s,bank,kind); size_t length; const void *before=asset_region(original,bank,kind,&length); lengths[kind]=length;
        first[bank][kind]=malloc(length); second[bank][kind]=malloc(length); CHECK(first[bank][kind]&&second[bank][kind]);
        asset_edit(s,kind,0); memcpy(first[bank][kind],asset_region(tm_studio_session_cart(s),bank,kind,&length),length);
        CHECK(memcmp(first[bank][kind],before,length)); asset_expect(s,expected,bank,kind,first[bank][kind],length);
        asset_edit(s,kind,1); memcpy(second[bank][kind],asset_region(tm_studio_session_cart(s),bank,kind,&length),length);
        CHECK(memcmp(second[bank][kind],first[bank][kind],length)); asset_expect(s,expected,bank,kind,second[bank][kind],length);
        asset_undo(s,kind,false); asset_expect(s,expected,bank,kind,first[bank][kind],length);
        CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
        CHECK(!memcmp(tm_studio_session_cart(s),expected,sizeof *expected));
        asset_undo(s,kind,true); asset_expect(s,expected,bank,kind,second[bank][kind],length);
        asset_undo(s,kind,false); asset_expect(s,expected,bank,kind,first[bank][kind],length);
        asset_undo(s,kind,false); asset_expect(s,expected,bank,kind,before,length);
        asset_undo(s,kind,true); asset_expect(s,expected,bank,kind,first[bank][kind],length);
        asset_undo(s,kind,true); asset_expect(s,expected,bank,kind,second[bank][kind],length);
        printf("Studio asset history bank=%u kind=%u: undone-position recovery, undo/redo and other assets retained\n",bank,kind);
    }
    CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(!memcmp(tm_studio_session_cart(s),expected,sizeof *expected));
    for(unsigned bank=0;bank<8;++bank) for(unsigned kind=0;kind<5;++kind) {
        asset_bank(s,bank,kind); asset_undo(s,kind,false); asset_expect(s,expected,bank,kind,first[bank][kind],lengths[kind]);
        asset_undo(s,kind,true); asset_expect(s,expected,bank,kind,second[bank][kind],lengths[kind]);
    }
    // RUN can acknowledge cart asset mutations without creating editor undo
    // nodes. Restoring the history must retain those accepted bytes until Undo.
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_a,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"function TIC() mset(0,0,85) sync(4,7,true) end\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
    expected->code=tm_studio_session_cart(s)->code;
    CHECK(!memcmp(tm_studio_session_cart(s),expected,sizeof *expected));
    asset_bank(s,7,1);
    CHECK(key(s,tic_key_r,tic_key_ctrl,1000)==TM_STUDIO_OK);
    // RUN initializes RAM from bank 0; sync(4,7,true) copies that full map.
    expected->banks[7].map=expected->banks[0].map;
    expected->banks[7].map.data[0]=85;
    CHECK(!memcmp(tm_studio_session_cart(s),expected,sizeof *expected));
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(!memcmp(tm_studio_session_cart(s),expected,sizeof *expected));
    asset_undo(s,1,false); asset_expect(s,expected,7,1,first[7][1],lengths[1]);
    asset_undo(s,1,true); asset_expect(s,expected,7,1,second[7][1],lengths[1]);
    for(unsigned bank=0;bank<8;++bank) for(unsigned kind=0;kind<5;++kind) { free(first[bank][kind]); free(second[bank][kind]); }
    free(original); free(expected);
    puts("Studio asset history recovery retains acknowledged RUN map changes until explicit Undo");
    puts("Studio asset histories: all 40 bank histories survive SIGKILL and hung RUN; independent undo/redo and full-cart preservation passed");
}
static void editor_code_recovery(tm_studio_session *s)
{
    char code[4096]="function TIC() while true do end end\n";
    for(unsigned i=0;i<40;++i) strcat(code,"-- scroll recovery line\n");
    strcat(code,"-- target ");
    for(unsigned i=0;i<100;++i) strcat(code,"x");
    strcat(code,"xyz");
    load(s,code);
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    for(unsigned i=0;i<3;++i) CHECK(key(s,tic_key_left,tic_key_shift,1000)==TM_STUDIO_OK);
    tm_studio_code_view view=*tm_studio_session_code_view(s);
    CHECK(view.cursor==(s32)strlen(code)-3 && view.selection==(s32)strlen(code));
    CHECK(view.scroll_x>0 && view.scroll_y>0);
    CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_mode(s)==TIC_CODE_MODE);
    CHECK(!memcmp(&view,tm_studio_session_code_view(s),sizeof view));
    CHECK(tm_studio_session_clipboard(s,"ABC")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
    memcpy(code+strlen(code)-3,"ABC",3);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    // A preferred column exceeds the actual column on a shorter line.
    // Preserve it so the next vertical move returns to the intended column.
    strcat(code,"\n-- x\n-- next abcdefghijklmnopqrstuvwxyz");
    load(s,code);
    CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_up,0,1000)==TM_STUDIO_OK);
    view=*tm_studio_session_code_view(s);
    CHECK(view.cursor==(s32)(strstr(code,"\n-- x\n")-code)+5);
    CHECK(view.column>4 && view.selection==-1);
    CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(!memcmp(&view,tm_studio_session_code_view(s),sizeof view));
    CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK);
    view=*tm_studio_session_code_view(s);
    CHECK(view.cursor==(s32)strlen(code));
    pid_t prior=tm_studio_session_pid(s); CHECK(kill(prior,SIGKILL)==0);
    CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(!memcmp(&view,tm_studio_session_code_view(s),sizeof view));
    CHECK(key(s,tic_key_q,0,1000)==TM_STUDIO_OK);
    strcat(code,"q"); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
    puts("Studio code recovery: cursor, selection, two-axis scroll and preferred column survive hung runs and SIGKILL");
}
static void code_undo_recovery(tm_studio_session *s)
{
    const char *original="function TIC() while true do end end\n";
    char first[256],second[256]; snprintf(first,sizeof first,"%s-- first edit\n",original);
    snprintf(second,sizeof second,"%s-- first edit\n-- second edit\n",original);
    load(s,original); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"-- first edit\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"-- second edit\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    CHECK(key(s,tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL));
    CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
    CHECK(key(s,tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,original));
    CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
    CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    puts("Studio code undo/redo history survives SIGKILL at an undone position and hung RUN");
}
static void clear_bookmarks(tm_studio_session *s)
{
    tic80_input input={0}; input.keyboard.keys[0]=tic_key_ctrl; input.keyboard.keys[1]=tic_key_shift; input.keyboard.keys[2]=tic_key_f1;
    CHECK(tick(s,input,1000)==TM_STUDIO_OK); idle(s,1);
}
static void bookmark_next(tm_studio_session *s,s32 expected)
{
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    if(tm_studio_session_code_view(s)->cursor!=expected)
        fprintf(stderr,"bookmark cursor=%d expected=%d\n",tm_studio_session_code_view(s)->cursor,expected);
    CHECK(tm_studio_session_code_view(s)->cursor==expected);
}
static void code_bookmarks(tm_studio_session *s)
{
    const char *code="function TIC() while true do end end\n-- alpha\n-- beta\n-- gamma\n";
    s32 alpha=strstr(code,"-- alpha")-code,beta=strstr(code,"-- beta")-code,gamma=strstr(code,"-- gamma")-code;
    load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->cursor==alpha);
    CHECK(key(s,tic_key_f1,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->cursor==beta);
    CHECK(key(s,tic_key_f1,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code) && !tm_studio_session_modified(s));
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    bookmark_next(s,alpha); bookmark_next(s,beta); bookmark_next(s,alpha);
    CHECK(key(s,tic_key_f1,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->cursor==beta);
    puts("Studio keyboard bookmarks survive SIGKILL; testing clear-all recovery");
    clear_bookmarks(s);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK); bookmark_next(s,0);
    puts("Studio bookmarks empty before SIGKILL; testing cleared snapshot");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    bookmark_next(s,0);
    puts("Studio cleared bookmarks remain empty after SIGKILL without text edits");
    // Upstream clear-all creates no undo node: Undo removes the last toggle,
    // leaving alpha; Redo restores both toggles. Preserve that exact behavior.
    CHECK(key(s,tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK); bookmark_next(s,alpha); bookmark_next(s,alpha);
    CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK); bookmark_next(s,alpha); bookmark_next(s,beta);
    clear_bookmarks(s); CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK); bookmark_next(s,0);
    // The existing margin toggle records history and must retain its mark too.
    mouse_click(s,2,TOOLBAR_SIZE+3*STUDIO_TEXT_HEIGHT+2);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK); bookmark_next(s,gamma);
    clear_bookmarks(s); clear_bookmarks(s);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK); bookmark_next(s,0);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code) && !tm_studio_session_modified(s));
    puts("Studio code bookmarks: keyboard/mouse marks, clear-all recovery, hung RUN and unchanged toggle undo/redo passed");
}
static void code_vi(tm_studio_session *s)
{
    const char *original="function TIC() while true do end end\n-- base";
    char first[128],second[128]; snprintf(first,sizeof first,"%scal",original);
    snprintf(second,sizeof second,"%sbc",first);
    s32 mark=strstr(original,"-- base")-original;
    load(s,original); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_g,tic_key_shift,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_i,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_c,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_a,0,1000)==TM_STUDIO_OK);
    CHECK(strlen(tm_studio_session_cart(s)->code.data)==strlen(original)+2);
    CHECK(!memcmp(tm_studio_session_cart(s)->code.data,original,strlen(original)));
    mouse_click(s,2,TOOLBAR_SIZE+STUDIO_TEXT_HEIGHT+2);
    puts("Studio Vi insert text and mouse bookmark acknowledged before SIGKILL");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
    CHECK(tm_studio_session_code_view(s)->vi_mode==VI_INSERT);
    puts("Studio Vi continued typing after SIGKILL; checking grouped undo and insert bookmark");
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_mode(s)==TIC_CODE_MODE);
    CHECK(tm_studio_session_code_view(s)->vi_mode==VI_NORMAL);
    CHECK(key(s,tic_key_g,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_period,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->cursor==mark);
    CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,original));
    CHECK(key(s,tic_key_u,tic_key_shift,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
    puts("Studio Vi first insert group is one undo/redo step and retains its bookmark");
    CHECK(key(s,tic_key_g,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_i,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_b,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_begin_pause(s)==TM_STUDIO_OK);
    int paused; do { paused=tm_studio_session_poll(s,1); } while(paused==TM_STUDIO_PENDING);
    CHECK(paused==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_c,0,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
    CHECK(key(s,tic_key_u,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    puts("Studio Vi explicit pause preserves unfinished insert and its separate undo group");
    CHECK(key(s,tic_key_g,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_v,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->selection==0 && tm_studio_session_code_view(s)->cursor==2);
    CHECK(tm_studio_session_code_view(s)->vi_mode==VI_SELECT);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->selection==0 && tm_studio_session_code_view(s)->cursor==3);
    CHECK(tm_studio_session_code_view(s)->vi_mode==VI_SELECT);
    CHECK(key(s,tic_key_d,0,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second+3));
    CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    puts("Studio Vi visual selection survives SIGKILL, extends and cuts the expected range");
    CHECK(key(s,tic_key_g,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_f,0,1000)==TM_STUDIO_OK);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->vi_mode==VI_SEEK);
    CHECK(key(s,tic_key_c,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->cursor==3);
    CHECK(tm_studio_session_code_view(s)->vi_mode==VI_NORMAL);
    CHECK(key(s,tic_key_f,tic_key_shift,1000)==TM_STUDIO_OK);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->vi_mode==VI_SEEK_BACK);
    CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->cursor==1);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    puts("Studio Vi forward/backward seek survives SIGKILL without inserting text");
    CHECK(key(s,tic_key_g,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_i,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_d,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_e,0,1000)==TM_STUDIO_OK);
    char third[128]; snprintf(third,sizeof third,"%sde",second);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,third));
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,second));
    CHECK(key(s,tic_key_u,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,third));
    CHECK(tm_studio_session_modified(s));
    puts("Studio Vi normal/insert/select/forward-seek/backward-seek recovery, grouped edits, insert bookmarks, explicit pause and hung RUN passed");
}
static void type_lower(tm_studio_session *s,const char *text)
{
    for(;*text;++text) {
        tic_key symbol=*text==' ' ? tic_key_space : *text>='a' && *text<='z' ? tic_key_a+*text-'a' : tic_key_0+*text-'0';
        CHECK(*text==' ' || (*text>='a' && *text<='z') || (*text>='0' && *text<='9'));
        CHECK(key(s,symbol,0,1000)==TM_STUDIO_OK);
    }
}
static void code_views(tm_studio_session *s)
{
    const char *code="function TIC() while true do end end\n-- alpha one\n-- alpha two\nfunction alpha() end\nfunction beta() end\n";
    load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_f,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12);
    CHECK(key(s,tic_key_a,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_p,0,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    puts("Studio FIND query and selected match acknowledged before SIGKILL");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_h,0,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    puts("Studio FIND query accepts more text after SIGKILL without editing the cart");
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_FIND);
    CHECK(!strcmp(tm_studio_session_code_view(s)->popup_text,"alph"));
    s32 one=strstr(code,"alpha one")-code,two=strstr(code,"alpha two")-code;
    CHECK(tm_studio_session_code_view(s)->cursor==one && tm_studio_session_code_view(s)->selection==one+4);
    CHECK(key(s,tic_key_a,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->cursor==two);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_up,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->cursor==one);
    CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK); idle(s,12);
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_right,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_right,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_right,tic_key_shift,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_f,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12);
    CHECK(key(s,tic_key_backspace,0,1000)==TM_STUDIO_OK); type_lower(s,"alpha");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); idle(s,20);
    CHECK(tm_studio_session_mode(s)==TIC_CODE_MODE && tm_studio_session_code_view(s)->mode==TM_CODE_EDIT);
    CHECK(tm_studio_session_code_view(s)->cursor==3 && tm_studio_session_code_view(s)->selection==2);
    puts("Studio FIND query, match navigation and ESC origin selection survive SIGKILL");
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_g,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12); type_lower(s,"3");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_GOTO && !strcmp(tm_studio_session_code_view(s)->popup_text,"3"));
    CHECK(key(s,tic_key_backspace,0,1000)==TM_STUDIO_OK); type_lower(s,"4");
    CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK); idle(s,12);
    s32 origin=strstr(code,"function alpha")-code; CHECK(tm_studio_session_code_view(s)->cursor==origin);
    CHECK(key(s,tic_key_g,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12);
    for(unsigned i=0;i<TM_CODE_POPUP_BYTES-1;++i) type_lower(s,"9");
    CHECK(tm_studio_session_code_view(s)->cursor==(s32)strlen(code) && tm_studio_session_code_view(s)->jump_line==5);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_backspace,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->jump_line==5);
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); idle(s,20); CHECK(tm_studio_session_code_view(s)->cursor==origin);
    puts("Studio GOTO digits, clamped maximum-length number and ESC origin survive SIGKILL");
    CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_f1,tic_key_ctrl,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_f1,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_b,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12); CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_BOOKMARK && tm_studio_session_code_view(s)->sidebar_count==2 && tm_studio_session_code_view(s)->sidebar_index==1);
    CHECK(key(s,tic_key_up,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK); idle(s,12);
    CHECK(tm_studio_session_code_view(s)->cursor==one-3);
    CHECK(key(s,tic_key_o,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12); type_lower(s,"al");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_OUTLINE && tm_studio_session_code_view(s)->sidebar_count==1);
    type_lower(s,"p"); CHECK(!strcmp(tm_studio_session_code_view(s)->popup_text,"alp"));
    CHECK(tm_studio_session_code_view(s)->cursor==(s32)(strstr(code,"alpha()")-code));
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); idle(s,20); CHECK(tm_studio_session_code_view(s)->cursor==one-3);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code) && !tm_studio_session_modified(s));
    puts("Studio bookmark selection and filtered outline rebuild from acknowledged text/history after SIGKILL");
    char large[4096]="function TIC() while true do end end\n";
    for(unsigned i=0;i<24;++i) { char line[64]; snprintf(line,sizeof line,"function fn%02u() end\n",i); strcat(large,line); }
    load(s,large); CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_o,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12); CHECK(key(s,tic_key_end,0,1000)==TM_STUDIO_OK);
    tm_studio_code_view view=*tm_studio_session_code_view(s); CHECK(view.sidebar_count==25 && view.sidebar_index==24 && view.sidebar_scroll>0);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->sidebar_index==24 && tm_studio_session_code_view(s)->sidebar_scroll==view.sidebar_scroll);
    CHECK(key(s,tic_key_up,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->cursor==(s32)(strstr(large,"fn22()")-large));
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); idle(s,20); CHECK(key(s,tic_key_home,tic_key_ctrl,1000)==TM_STUDIO_OK);
    for(unsigned i=0;i<24;++i) { CHECK(key(s,tic_key_down,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_f1,tic_key_ctrl,1000)==TM_STUDIO_OK); }
    CHECK(key(s,tic_key_b,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,12); CHECK(key(s,tic_key_end,0,1000)==TM_STUDIO_OK);
    view=*tm_studio_session_code_view(s); CHECK(view.sidebar_count==24 && view.sidebar_index==23 && view.sidebar_scroll>0);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->sidebar_index==23 && tm_studio_session_code_view(s)->sidebar_scroll==view.sidebar_scroll);
    CHECK(key(s,tic_key_up,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK); idle(s,12);
    CHECK(tm_studio_session_code_view(s)->cursor==(s32)(strstr(large,"function fn22")-large));
    puts("Studio long bookmark/outline sidebar index and nonzero scroll survive SIGKILL and keep navigation working");
    CHECK(key(s,tic_key_f,tic_key_ctrl,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->animation==TM_CODE_SHOW);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    type_lower(s,"fn"); idle(s,12); CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_FIND);
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->animation==TM_CODE_HIDE);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    idle(s,20); CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_EDIT);
    mouse_click(s,206,1); idle(s,12); CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_DRAG);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_DRAG); mouse_click(s,206,1); idle(s,12);
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_EDIT);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,large) && !tm_studio_session_modified(s));
    puts("Studio popup show/hide transitions and drag-tool selection survive SIGKILL without a stuck view");
}
static void code_modal_routing(tm_studio_session *s)
{
    const char *code="function TIC() while true do end end\nfunction alpha() end\n-- alpha base";
    const unsigned vi_modes[]={VI_INSERT,VI_SELECT,VI_SEEK,VI_SEEK_BACK,VI_NORMAL};
    const unsigned modes[]={TM_CODE_FIND,TM_CODE_GOTO,TM_CODE_BOOKMARK,TM_CODE_OUTLINE,TM_CODE_DRAG};
    const unsigned tool_x[]={213,220,227,234,206};
    for(unsigned v=0;v<5;++v) for(unsigned m=0;m<5;++m) {
        unsigned vi=vi_modes[v],mode=modes[m];
        load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_g,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_m,0,1000)==TM_STUDIO_OK);
        char expected[256]; strcpy(expected,code);
        switch(vi) {
        case VI_INSERT:
            CHECK(key(s,tic_key_g,tic_key_shift,1000)==TM_STUDIO_OK);
            CHECK(key(s,tic_key_i,0,1000)==TM_STUDIO_OK); type_lower(s,"ca"); strcat(expected,"ca"); break;
        case VI_SELECT:
            CHECK(key(s,tic_key_v,0,1000)==TM_STUDIO_OK);
            CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK); break;
        case VI_SEEK: CHECK(key(s,tic_key_f,0,1000)==TM_STUDIO_OK); break;
        case VI_SEEK_BACK:
            for(unsigned i=0;i<3;++i) CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK);
            CHECK(key(s,tic_key_f,tic_key_shift,1000)==TM_STUDIO_OK); break;
        default: CHECK(key(s,tic_key_g,tic_key_shift,1000)==TM_STUDIO_OK); break;
        }
        tm_studio_code_view origin=*tm_studio_session_code_view(s);
        CHECK(origin.vi_mode==vi && origin.mode==TM_CODE_EDIT);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected));
        mouse_click(s,tool_x[m],1); idle(s,12);
        CHECK(tm_studio_session_code_view(s)->mode==mode && tm_studio_session_code_view(s)->vi_mode==vi);
        if(mode==TM_CODE_FIND || mode==TM_CODE_OUTLINE) {
            while(*tm_studio_session_code_view(s)->popup_text) CHECK(key(s,tic_key_backspace,0,1000)==TM_STUDIO_OK);
            type_lower(s,"alpha");
        } else if(mode==TM_CODE_GOTO) type_lower(s,"2");
        printf("Studio modal routing vi=%u view=%u acknowledged before SIGKILL\n",vi,mode);
        CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
        CHECK(tm_studio_session_code_view(s)->mode==mode && tm_studio_session_code_view(s)->vi_mode==vi);
        CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); idle(s,20);
        CHECK(tm_studio_session_mode(s)==TIC_CODE_MODE && tm_studio_session_code_view(s)->mode==TM_CODE_EDIT);
        CHECK(tm_studio_session_code_view(s)->vi_mode==vi);
        CHECK(tm_studio_session_code_view(s)->cursor==origin.cursor && tm_studio_session_code_view(s)->selection==origin.selection);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected));
        switch(vi) {
        case VI_INSERT:
            type_lower(s,"d"); strcat(expected,"d"); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected));
            CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->vi_mode==VI_NORMAL);
            CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
            CHECK(key(s,tic_key_u,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected)); break;
        case VI_SELECT:
            CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK);
            CHECK(tm_studio_session_code_view(s)->cursor==3 && tm_studio_session_code_view(s)->selection==0);
            CHECK(key(s,tic_key_d,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code+3));
            CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code)); break;
        case VI_SEEK:
            CHECK(key(s,tic_key_c,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->cursor==3); break;
        case VI_SEEK_BACK:
            CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_code_view(s)->cursor==1); break;
        default:
            CHECK(key(s,tic_key_g,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_l,0,1000)==TM_STUDIO_OK);
            CHECK(tm_studio_session_code_view(s)->cursor==1); break;
        }
        CHECK(tm_studio_session_code_view(s)->vi_mode==VI_NORMAL);
        CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
        printf("Studio modal routing vi=%u view=%u: cancel, underlying command and console Escape passed\n",vi,mode);
    }
    puts("Studio modal routing: all 25 toolbar-view/Vi combinations survive SIGKILL and cancel without consuming the underlying Vi Escape");
    for(unsigned m=0;m<5;++m) {
        load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_g,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_m,0,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_g,tic_key_shift,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_i,0,1000)==TM_STUDIO_OK); type_lower(s,"ca");
        tm_studio_code_view origin=*tm_studio_session_code_view(s);
        mouse_click(s,tool_x[m],1);
        CHECK(tm_studio_session_code_view(s)->mode==modes[m] && tm_studio_session_code_view(s)->animation==TM_CODE_SHOW);
        CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_code_view(s)->animation==TM_CODE_HIDE && tm_studio_session_code_view(s)->vi_mode==VI_INSERT);
        CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
        CHECK(tm_studio_session_code_view(s)->animation==TM_CODE_HIDE); idle(s,20);
        CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_EDIT && tm_studio_session_code_view(s)->vi_mode==VI_INSERT);
        CHECK(tm_studio_session_code_view(s)->cursor==origin.cursor && tm_studio_session_code_view(s)->selection==origin.selection);
        type_lower(s,"d"); char expected[256]; snprintf(expected,sizeof expected,"%scad",code);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected));
        CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
        CHECK(key(s,tic_key_u,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected));
        printf("Studio modal routing view=%u: Escape during opening and SIGKILL during closing retain one insert group\n",modes[m]);
    }
    load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    mouse_click(s,206,1); idle(s,12);
    tm_input_state mouse={0}; tic80_input input={0};
    tm_input_snapshot snapshot={.mouse=120|(64<<8)|(1u<<16)};
    tm_input_convert(&mouse,&snapshot,&input); CHECK(tick(s,input,1000)==TM_STUDIO_OK);
    snapshot.mouse=132|(70<<8)|(1u<<16); tm_input_convert(&mouse,&snapshot,&input);
    CHECK(tick(s,input,1000)==TM_STUDIO_OK);
    tm_studio_code_view dragged=*tm_studio_session_code_view(s);
    input.keyboard.keys[0]=tic_key_escape; CHECK(tick(s,input,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->animation==TM_CODE_HIDE);
    for(unsigned i=0;i<5;++i) {
        snapshot.mouse=(140+i*2)|((80+i*2)<<8)|(1u<<16);
        input=(tic80_input){0}; tm_input_convert(&mouse,&snapshot,&input); CHECK(tick(s,input,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_code_view(s)->animation==TM_CODE_HIDE);
        CHECK(tm_studio_session_code_view(s)->scroll_x==dragged.scroll_x && tm_studio_session_code_view(s)->scroll_y==dragged.scroll_y);
        CHECK(tm_studio_session_code_view(s)->cursor==dragged.cursor && tm_studio_session_code_view(s)->selection==dragged.selection);
    }
    snapshot.mouse=120|(64<<8); input=(tic80_input){0}; tm_input_convert(&mouse,&snapshot,&input);
    CHECK(tick(s,input,1000)==TM_STUDIO_OK); idle(s,20);
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_EDIT);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    puts("Studio modal routing: held mouse cannot restart dragging during Escape closing");
    for(unsigned hung=0;hung<2;++hung) {
        load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_g,tic_key_shift,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_i,0,1000)==TM_STUDIO_OK); type_lower(s,"ca");
        tm_studio_code_view origin=*tm_studio_session_code_view(s);
        mouse_click(s,213,1); idle(s,12); type_lower(s,"alpha");
        if(hung) CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
        else {
            CHECK(tm_studio_session_begin_pause(s)==TM_STUDIO_OK);
            int paused; do { paused=tm_studio_session_poll(s,1); } while(paused==TM_STUDIO_PENDING);
            CHECK(paused==TM_STUDIO_RECOVERED);
        }
        CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_FIND && tm_studio_session_code_view(s)->vi_mode==VI_INSERT);
        CHECK(!strcmp(tm_studio_session_code_view(s)->popup_text,"alpha"));
        CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK); idle(s,20);
        CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_EDIT && tm_studio_session_code_view(s)->vi_mode==VI_INSERT);
        CHECK(tm_studio_session_code_view(s)->cursor==origin.cursor);
        type_lower(s,"d"); char expected[256]; snprintf(expected,sizeof expected,"%scad",code);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected));
        CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
        CHECK(key(s,tic_key_u,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,expected));
        printf("Studio modal routing: %s retains search cancellation and unfinished insert undo group\n",hung?"hung RUN":"explicit pause");
    }
}
static void code_replace_bounds(tm_studio_session *s)
{
    char code[256]="function TIC() while true do end end\n-- ";
    for(unsigned i=0;i<TM_CODE_POPUP_BYTES-1;++i) strcat(code,"a");
    load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_r,0,1000)==TM_STUDIO_OK); idle(s,12);
    for(unsigned i=0;i<TM_CODE_POPUP_BYTES-1;++i) type_lower(s,"a");
    puts("Studio maximum-length REPLACE search acknowledged; Enter must remain bounded");
    CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->mode==TM_CODE_REPLACE && tm_studio_session_code_view(s)->replace_offset==-1);
    CHECK(strlen(tm_studio_session_code_view(s)->popup_text)==TM_CODE_POPUP_BYTES-1);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    for(unsigned i=0;i<6;++i) CHECK(key(s,tic_key_backspace,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_code_view(s)->replace_offset==27 && strlen(tm_studio_session_code_view(s)->popup_text)==33);
    type_lower(s,"z"); CHECK(strlen(tm_studio_session_code_view(s)->popup_text)==33);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK); idle(s,12);
    code[strlen(code)-27]=0; CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    puts("Studio REPLACE popup rejects oversized label, permits exact boundary and empty replacement after SIGKILL");
}
static void code_replace_views(tm_studio_session *s)
{
    const char *code="function TIC() while true do end end\n-- alpha alpha\n-- alpha\n";
    const char *changed="function TIC() while true do end end\n-- beta beta\n-- beta\n";
    load(s,code); idle(s,120); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_r,0,1000)==TM_STUDIO_OK); idle(s,12); type_lower(s,"alp");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    type_lower(s,"ha"); CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK); type_lower(s,"be");
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL)); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_code_view(s)->replace_offset==5 && !strcmp(tm_studio_session_code_view(s)->popup_text,"alpha WITH:be"));
    type_lower(s,"ta"); CHECK(key(s,tic_key_backspace,0,1000)==TM_STUDIO_OK); type_lower(s,"a");
    CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_OK); idle(s,12);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,changed));
    CHECK(key(s,tic_key_u,0,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,code));
    CHECK(key(s,tic_key_u,tic_key_shift,1000)==TM_STUDIO_OK); CHECK(!strcmp(tm_studio_session_cart(s)->code.data,changed));
    puts("Studio REPLACE search and replacement stages survive SIGKILL, perform intended bulk edit and retain undo/redo");
    code_replace_bounds(s);
}
static unsigned descriptor_count(void)
{
    DIR *folder=opendir("/proc/self/fd"); CHECK(folder); unsigned count=0; struct dirent *entry;
    while((entry=readdir(folder))) if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) ++count;
    CHECK(!closedir(folder)); return count;
}
static void history_ack_faults(const char *folder)
{
    char marker[512]; snprintf(marker,sizeof marker,"%s/history-fault",folder);
    CHECK(!setenv("TM_TEST_HISTORY_FAULT",marker,1));
    unsigned descriptors=descriptor_count();
    for(const char *action="DTCMXUW";*action;++action) {
        tm_studio_session *s=NULL; CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
        const char *original="function TIC() end\n"; char first[128]; snprintf(first,sizeof first,"%s-- accepted\n",original);
        load(s,original); CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
        CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_clipboard(s,"-- accepted\n")==TM_STUDIO_OK);
        CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
        CHECK(tm_studio_session_clipboard(s,"-- rejected\n")==TM_STUDIO_OK);
        FILE *fault=fopen(marker,"wb"); CHECK(fault); CHECK(fputc(*action,fault)!=EOF); CHECK(!fclose(fault));
        int recovered=key(s,tic_key_v,tic_key_ctrl,1000);
        if(recovered!=TM_STUDIO_RECOVERED)
            fprintf(stderr,"history ACK fault %c result=%d mode=%d worker=%ld marker=%d\n",*action,recovered,tm_studio_session_mode(s),(long)tm_studio_session_pid(s),access(marker,F_OK));
        CHECK(recovered==TM_STUDIO_RECOVERED);
        CHECK(access(marker,F_OK)==-1 && errno==ENOENT);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
        CHECK(key(s,tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,original));
        CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
        CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
        CHECK(!strcmp(tm_studio_session_cart(s)->code.data,first));
        CHECK(tm_studio_session_close(s)==TM_STUDIO_OK); CHECK(descriptor_count()==descriptors);
        printf("Studio history acknowledgement fault %c rejected; accepted undo retained and descriptors reclaimed\n",*action);
    }
    CHECK(!unsetenv("TM_TEST_HISTORY_FAULT"));
}
static void editor_mouse(tm_studio_session *s)
{
    load(s,"function TIC() while true do end end\n");
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    const EditorMode modes[]={TIC_CODE_MODE,TIC_SPRITE_MODE,TIC_MAP_MODE,TIC_SFX_MODE,TIC_MUSIC_MODE};
    // Hit the top/left pixel of each toolbar tab through the actual FPGA
    // coordinate converter. Studio widgets use the 240x136 game coordinates.
    for(unsigned i=0;i<5;++i) {
        mouse_click(s,i*TOOLBAR_SIZE,0); CHECK(tm_studio_session_mode(s)==modes[i]);
    }
    mouse_click(s,TOOLBAR_SIZE,0); CHECK(tm_studio_session_mode(s)==TIC_SPRITE_MODE);
    unsigned bank_x=5*TOOLBAR_SIZE+2;
    mouse_click(s,bank_x+1,1); // open the bank row
    mouse_click(s,bank_x+2+8*TOOLBAR_SIZE+1,1); // select bank 7
    mouse_click(s,24+5*8+4,112+4); // palette color 5
    mouse_click(s,24+4,20+4); // first pixel of the selected sprite
    CHECK(sprite_pixel(s,7)==5);
    CHECK(sprite_pixel(s,0)==0);
    CHECK(tm_studio_session_modified(s));
    CHECK(key(s,tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(sprite_pixel(s,7)==0);
    CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(sprite_pixel(s,7)==5);
    CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_mode(s)==TIC_SPRITE_MODE);
    CHECK(key(s,tic_key_z,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(sprite_pixel(s,7)==0);
    CHECK(key(s,tic_key_y,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(sprite_pixel(s,7)==5);
    mouse_click(s,24+12,20+4); // continue with the previously selected color
    CHECK((tm_studio_session_cart(s)->banks[7].tiles.data[1].data[0]>>4)==5);
    mouse_click(s,24+6*8+4,112+4); // another edit after recovery
    mouse_click(s,24+4,20+4);
    CHECK(sprite_pixel(s,7)==6);
    CHECK(sprite_pixel(s,0)==0);
    mouse_click(s,111+5*8+3,TOOLBAR_SIZE+3); // select sprite 5
    mouse_click(s,24+3*8+4,112+8+4); // palette color 11
    CHECK(key(s,tic_key_4,0,1000)==TM_STUDIO_OK); // fill tool
    CHECK(key(s,tic_key_r,tic_key_ctrl,250)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_mode(s)==TIC_SPRITE_MODE);
    mouse_click(s,24+4,20+4);
    CHECK(tm_studio_session_cart(s)->banks[7].tiles.data[5].data[0]==0xbb);
    CHECK(tm_studio_session_cart(s)->banks[7].tiles.data[5].data[31]==0xbb);
    CHECK(sprite_pixel(s,7)==6); // sprite 1 must remain untouched
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
    puts("Studio MiSTer mouse: five toolbar tabs at y=0, bank 7 selection, sprite painting, undo/redo and bank/sprite/color/tool recovery passed");
}
static int remove_entry(const char *path,const struct stat *info,int kind,struct FTW *walk)
{ (void)info; (void)kind; (void)walk; return remove(path); }
static void vi_options(const char *folder)
{
    char local[512],version[512],options[512];
    snprintf(local,sizeof local,"%s/%s",folder,TIC_LOCAL); CHECK(!mkdir(local,0700) || errno==EEXIST);
    snprintf(version,sizeof version,"%s/%s",folder,TIC_LOCAL_VERSION); CHECK(!mkdir(version,0700) || errno==EEXIST);
    snprintf(options,sizeof options,"%s/options.json",version);
    FILE *file=fopen(options,"wb"); CHECK(file); CHECK(fputs("{\"keybindMode\":2}",file)>=0); CHECK(!fclose(file));
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    if(argc==2 && !strcmp(argv[1],"--modal-routing")) {
        CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
        char folder[]="/tmp/tic80-studio-modal-XXXXXX"; CHECK(mkdtemp(folder)); vi_options(folder);
        tm_studio_session *s=NULL; CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
        code_modal_routing(s); CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
        CHECK(nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0); return 0;
    }
    if(argc==2 && (!strcmp(argv[1],"--code-views") || !strcmp(argv[1],"--replace-bounds"))) {
        CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
        char folder[]="/tmp/tic80-studio-code-views-XXXXXX"; CHECK(mkdtemp(folder));
        tm_studio_session *s=NULL;
        if(!strcmp(argv[1],"--code-views")) {
            CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
            code_views(s); CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
        }
        vi_options(folder); CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
        if(!strcmp(argv[1],"--code-views")) code_replace_views(s); else code_replace_bounds(s);
        CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
        CHECK(nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0); return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--vi")) {
        CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
        char folder[]="/tmp/tic80-studio-vi-XXXXXX"; CHECK(mkdtemp(folder));
        vi_options(folder);
        tm_studio_session *s=NULL; CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
        code_vi(s); CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
        CHECK(nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0); return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--bookmarks")) {
        CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
        char folder[]="/tmp/tic80-studio-bookmarks-XXXXXX"; CHECK(mkdtemp(folder));
        tm_studio_session *s=NULL; CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
        code_bookmarks(s); CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
        CHECK(nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0); return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--asset-history")) {
        CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
        char folder[]="/tmp/tic80-studio-assets-XXXXXX"; CHECK(mkdtemp(folder));
        tm_studio_session *s=NULL; CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK);
        asset_histories(s); CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
        CHECK(nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0); return 0;
    }
    CHECK(argc==1 || (argc==3 && !strcmp(argv[1],"--benchmark-cart")));
    char folder[]="/tmp/tic80-studio-session-XXXXXX"; CHECK(mkdtemp(folder));
    tm_studio_session *s=NULL;
    CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK && s);
    // Programmatic launch must work before the console's startup animation
    // enables keyboard shortcuts, including first-BOOT fault recovery.
    load(s,"function BOOT() while true do end end function TIC() cls(6) end\n");
    pid_t cold=tm_studio_session_pid(s);
    CHECK(tm_studio_session_run(s,250)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_pid(s)!=cold && tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
    CHECK(strstr(tm_studio_session_cart(s)->code.data,"while true"));
    load(s,"function TIC() cls(6) end\n");
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_OK && tm_studio_session_mode(s)==TIC_RUN_MODE);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    CHECK(tm_studio_session_open(&s,folder)==TM_STUDIO_OK && s);
    idle(s,120); CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
    editor_mouse(s);
    code_undo_recovery(s);
    history_ack_faults(folder);
    editor_code_recovery(s);
    load(s,"function TIC() cls(6) end\n");
    CHECK(key(s,tic_key_r,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_mode(s)==TIC_RUN_MODE); idle(s,3);
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_mode(s)==TIC_CONSOLE_MODE);
    load(s,"function TIC() while true do end end\n");
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_mode(s)==TIC_CODE_MODE);
    const char edited[]="function TIC() while true do end end\n-- unsaved edit\n";
    CHECK(tm_studio_session_clipboard(s,edited)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_a,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,edited));
    CHECK(tm_studio_session_modified(s));
    tic_cartridge *checkpoint=malloc(sizeof *checkpoint); CHECK(checkpoint);
    *checkpoint=*tm_studio_session_cart(s); pid_t prior=tm_studio_session_pid(s);
    // Ctrl+R can hang inside its own first tick; the preceding ACK must be enough.
    tic80_input run={0}; run.keyboard.keys[0]=tic_key_ctrl; run.keyboard.keys[1]=tic_key_r;
    int result=tick(s,run,250);
    if(result==TM_STUDIO_OK) result=tick(s,(tic80_input){0},250);
    CHECK(result==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_pid(s)!=prior && tm_studio_session_mode(s)==TIC_CODE_MODE);
    if(memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint)) {
        unsigned printed=0;
        for(size_t i=0;i<sizeof *checkpoint && printed<16;++i)
            if(((u8*)checkpoint)[i]!=((const u8*)tm_studio_session_cart(s))[i]) {
                fprintf(stderr,"checkpoint diff %zu: %u -> %u\n",i,((u8*)checkpoint)[i],((const u8*)tm_studio_session_cart(s))[i]); ++printed;
            }
    }
    CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(!strcmp(tm_studio_session_name(s)->name,"checkpoint.tic"));
    CHECK(tm_studio_session_modified(s));
    // Holding the trigger after recovery must not immediately launch another hang.
    CHECK(tick(s,run,250)==TM_STUDIO_OK && tm_studio_session_mode(s)==TIC_CODE_MODE);
    idle(s,1);
    prior=tm_studio_session_pid(s); CHECK(kill(prior,SIGKILL)==0);
    CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_pid(s)!=prior);
    CHECK(tm_studio_session_modified(s));
    // Verify the clipboard survived a crash by clearing and pasting again.
    CHECK(key(s,tic_key_a,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_backspace,0,1000)==TM_STUDIO_OK);
    CHECK(!tm_studio_session_cart(s)->code.data[0]);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,edited));
    // Bounded malformed loads preserve the checkpoint and worker.
    prior=tm_studio_session_pid(s);
    CHECK(tm_studio_session_load(s,(const u8*)"broken",6,"bad.tic")==TM_STUDIO_ERROR);
    CHECK(tm_studio_session_begin_load(s,(const u8*)"broken",6,"bad.tic",1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_begin_run(s,1000)==TM_STUDIO_ERROR);
    int rejected;
    do {
        CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
        rejected=tm_studio_session_poll(s,0);
        if(rejected==TM_STUDIO_PENDING) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
    } while(rejected==TM_STUDIO_PENDING);
    CHECK(rejected==TM_STUDIO_CART_ERROR);
    CHECK(tm_studio_session_pid(s)==prior && !memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_clipboard(s,"-- recovered clipboard")==TM_STUDIO_OK);
    // A running tick mutates both cartridge and persistent memory, then hangs.
    // Recover only the previous completed state, not the partial mutation.
    load(s,"function TIC() pmem(0,pmem(0)+1) if btn(4) then mset(0,0,77) sync(4,7,true) while true do end end end\n");
    CHECK(key(s,tic_key_r,tic_key_ctrl,1000)==TM_STUDIO_OK); idle(s,3);
    *checkpoint=*tm_studio_session_cart(s);
    u32 persistent=tm_studio_session_persistent(s)[0]; CHECK(persistent>0);
    tic80_input trigger={0}; trigger.gamepads.data=1u<<4;
    CHECK(tm_studio_session_begin_tick(s,trigger,frames++*1000000000ULL/60,250)==TM_STUDIO_OK);
    CHECK(tm_studio_session_begin_tick(s,(tic80_input){0},0,1000)==TM_STUDIO_ERROR);
    CHECK(tm_studio_session_clipboard(s,"must not overwrite an outstanding request")==TM_STUDIO_ERROR);
    CHECK(tm_studio_session_run(s,1000)==TM_STUDIO_ERROR);
    unsigned pending_polls=0; uint64_t deadline=counter()+6000000000ULL;
    do {
        result=tm_studio_session_poll(s,0);
        if(result==TM_STUDIO_PENDING) {
            ++pending_polls;
            CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
            CHECK(tm_studio_session_persistent(s)[0]==persistent);
            struct timespec delay={0,2000000}; nanosleep(&delay,NULL);
        }
        CHECK(counter()<deadline);
    } while(result==TM_STUDIO_PENDING);
    CHECK(result==TM_STUDIO_RECOVERED && pending_polls>10);
    CHECK(tm_studio_session_mode(s)==TIC_CODE_MODE);
    CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_persistent(s)[0]==persistent);
    idle(s,30);
    // Explicit reset cancellation must recover a partial hung tick before
    // its execution deadline, without committing the worker's mutations.
    CHECK(tm_studio_session_begin_run(s,1000)==TM_STUDIO_OK);
    do { result=tm_studio_session_poll(s,10); } while(result==TM_STUDIO_PENDING);
    CHECK(result==TM_STUDIO_OK);
    *checkpoint=*tm_studio_session_cart(s); persistent=tm_studio_session_persistent(s)[0];
    CHECK(tm_studio_session_begin_tick(s,trigger,UINT64_MAX,5000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_begin_pause(s)==TM_STUDIO_OK);
    deadline=counter()+3000000000ULL;
    do {
        CHECK(!memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
        CHECK(tm_studio_session_persistent(s)[0]==persistent);
        result=tm_studio_session_poll(s,10); CHECK(counter()<deadline);
    } while(result==TM_STUDIO_PENDING);
    CHECK(result==TM_STUDIO_RECOVERED && tm_studio_session_mode(s)==TIC_CODE_MODE);
    // Cancel a valid LOAD before its ACK is accepted. The old cartridge,
    // editor and pmem remain the checkpoint even if decoding already ended.
    const u8 replacement[]={17,0,0,0,5,25,0,0,'f','u','n','c','t','i','o','n',' ','T','I','C','(',')',' ','c','l','s','(','2',')',' ','e','n','d'};
    CHECK(tm_studio_session_begin_load(s,replacement,sizeof replacement,"cancelled.tic",5000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_begin_pause(s)==TM_STUDIO_OK);
    deadline=counter()+3000000000ULL;
    do { result=tm_studio_session_poll(s,10); CHECK(counter()<deadline); } while(result==TM_STUDIO_PENDING);
    CHECK(result==TM_STUDIO_RECOVERED && !memcmp(checkpoint,tm_studio_session_cart(s),sizeof *checkpoint));
    CHECK(tm_studio_session_persistent(s)[0]==persistent);
    if(argc==3) {
        FILE *file=fopen(argv[2],"rb"); CHECK(file);
        CHECK(!fseek(file,0,SEEK_END)); long size=ftell(file); CHECK(size>0 && size<=4*1024*1024);
        rewind(file); u8 *data=malloc((size_t)size); CHECK(data);
        CHECK(fread(data,1,(size_t)size,file)==(size_t)size && !fclose(file));
        CHECK(tm_studio_session_load(s,data,(size_t)size,"benchmark.tic")==TM_STUDIO_OK); free(data);
    } else load(s,"function TIC() cls(6) end\n");
    CHECK(key(s,tic_key_r,tic_key_ctrl,1000)==TM_STUDIO_OK);
    if(argc==3) { tic80_input start={0}; start.gamepads.data=1u<<4; CHECK(tick(s,start,1000)==TM_STUDIO_OK); }
    idle(s,30);
    uint64_t durations[600],total=0; unsigned over_budget=0;
    for(unsigned i=0;i<600;++i) {
        uint64_t started=counter(); CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_OK);
        durations[i]=counter()-started; total+=durations[i]; over_budget+=durations[i]>16666667;
    }
    qsort(durations,600,sizeof *durations,compare_time);
    printf("Studio supervision timing: 600 RUN ticks, cart_size=%zu, mean=%.3f ms p50=%.3f ms p95=%.3f ms p99=%.3f ms max=%.3f ms over_60hz_budget=%u\n",
        sizeof(tic_cartridge),total/600000000.0,durations[299]/1e6,durations[569]/1e6,durations[593]/1e6,durations[599]/1e6,over_budget);
    CHECK(tm_studio_session_begin_tick(s,(tic80_input){0},UINT64_MAX,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK); free(checkpoint);
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    CHECK(nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
    puts("Studio supervision: unsaved cartridge/bank/name/clipboard checkpoint, cold/late hangs, partial mutation, pmem, held-trigger suppression, SIGKILL, bounded load, run/return, asynchronous recovery and child cleanup passed");
    return 0;
}
