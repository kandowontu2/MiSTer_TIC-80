#define _XOPEN_SOURCE 700
#include "tic80_mister/input.h"
#include "tic80_mister/studio.h"
#include "studio/studio.h"
#include "studio/config.h"
#include "cart.h"
#include "api.h"
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static u64 frames;
static u64 counter(void *data) { (void)data; return frames; }
static u64 frequency(void *data) { (void)data; return 60; }
static void error(const char *text) { fprintf(stderr,"%s\n",text); exit(1); }
static void tick(Studio *studio,tic80_input input)
{
    tm_studio_clock(frames*1000000000ULL/60);
    tm_studio_tick(studio,input); studio_sound(studio);
    CHECK(!studio_alive(studio));
}
static int cleanup(const char *path,const struct stat *st,int kind,struct FTW *walk)
{ (void)st; (void)kind; (void)walk; return remove(path); }
int main(void)
{
    char folder[]="/tmp/tic80-input-frontends-XXXXXX"; CHECK(mkdtemp(folder));
    char *args[]={"tic80-studio","--skip",NULL};
    Studio *studio=studio_create(2,args,48000,TIC80_PIXEL_COLOR_RGBA8888,folder,1,tic_layout_qwerty);
    CHECK(studio); tm_studio_bind(studio);
    studio_config_get(studio)->data.checkNewVersion=false;
    for(frames=0;frames<120;++frames) tick(studio,(tic80_input){0});
    CHECK(getStudioMode(studio)==TIC_CONSOLE_MODE);
    tic_cartridge *cart=&getMemory(studio)->cart;
    memset(cart,0,sizeof *cart);
    strcpy(cart->code.data,"-- script: lua\nfunction TIC() local x,y,l,m,r,sx,sy=mouse(); pmem(0,sx+32); pmem(1,pmem(1)+sx); if sx>0 then pmem(2,pmem(2)+sx) end if sx<0 then pmem(3,pmem(3)-sx) end pmem(4,sy+32) end\n");
    u8 *bytes=malloc(2*sizeof *cart); CHECK(bytes);
    s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
    tic80 *player=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(player);
    player->callback.error=error; tic80_load(player,bytes,size); free(bytes);
    setStudioMode(studio,TIC_CODE_MODE);
    tick(studio,(tic80_input){0});
    tm_studio_code_view before,after;
    tm_studio_code_state(studio,&before);
    tic80_input right={0}; right.mouse.scrollx=3;
    tick(studio,right); tm_studio_code_state(studio,&after);
    CHECK(after.scroll_x>before.scroll_x);
    right.mouse.scrollx=-3; tick(studio,right);
    tm_studio_code_state(studio,&after); CHECK(after.scroll_x==before.scroll_x);
    runGame(studio,RUN_FROM_STUDIO); CHECK(getStudioMode(studio)==TIC_RUN_MODE);
    tm_input_state ss={0},ps={0}; tm_input_snapshot snapshot={0};
    // Independent expected API signs, including both burst limits and wrap.
    const int deltas[]={0,3,-5,65,0,0,-63,0,0,0,32,0,-32,0};
    const int expected[]={0,-3,5,-31,-31,-3,31,31,1,0,-31,-1,31,1};
    int sum=0,positive=0,negative=0;
    snapshot.horizontal_wheel=0xfffffffe;
    for(unsigned n=0;n<sizeof deltas/sizeof *deltas;++n,++frames) {
        snapshot.horizontal_wheel+=(uint32_t)deltas[n];
        tic80_input si={0},pi={0};
        tm_input_convert(&ss,&snapshot,&si);
        tm_input_convert_player(&ps,&snapshot,&pi);
        tick(studio,si); tic80_tick(player,pi,counter,frequency); tic80_sound(player);
        CHECK(getStudioMode(studio)==TIC_RUN_MODE);
        sum+=expected[n]; if(expected[n]>0) positive+=expected[n]; else negative-=expected[n];
        const uint32_t values[]={(uint32_t)(expected[n]+32),(uint32_t)sum,(uint32_t)positive,(uint32_t)negative,32};
        for(unsigned p=0;p<5;++p) {
            CHECK(tic_api_pmem(getMemory(studio),p,0,false)==values[p]);
            CHECK(tic_api_pmem((tic_mem*)player,p,0,false)==values[p]);
        }
    }
    CHECK(!ss.pending_horizontal_wheel && !ps.pending_horizontal_wheel);
    tic80_delete(player); studio_delete(studio); tm_studio_bind(NULL);
    CHECK(!nftw(folder,cleanup,16,FTW_DEPTH|FTW_PHYS));
    puts("Actual Studio RUN and library player mouse(): direction, wrap and both burst limits agree without lost or reversed detents");
    return 0;
}
