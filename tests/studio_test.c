#define _XOPEN_SOURCE 700
#include "tic80_mister/studio.h"
#include "studio/studio.h"
#include "studio/config.h"
#include "studio/editors/sprite.h"
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)

static uint64_t frames;
static const char *screens;
static void tick(Studio *studio,tic80_input input)
{
    tm_studio_clock(frames++*1000000000ULL/60);
    tm_studio_tick(studio,input); studio_sound(studio);
    /* The pinned platform API uses true for an exit request despite its name. */
    CHECK(!studio_alive(studio));
    CHECK(studio_mem(studio)->product.samples.count==1600);
}
static void idle(Studio *studio,unsigned count)
{ while(count--) tick(studio,(tic80_input){0}); }
static void key(Studio *studio,tic_key key,tic_key modifier)
{
    tic80_input input={0};
    input.keyboard.keys[0]=modifier;
    input.keyboard.keys[1]=key;
    tick(studio,input); idle(studio,1);
}
static void type(Studio *studio,const char *text)
{
    const char symbols[]=" abcdefghijklmnopqrstuvwxyz0123456789-=[]\\;'`,./ ";
    const char shifted[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ)!@#$%^&*(_+{}|:\"~<>? ";
    for(;*text;++text) {
        if(*text=='\n') { key(studio,tic_key_return,0); continue; }
        const char *pos=strchr(symbols+1,*text);
        if(pos) key(studio,(tic_key)(pos-symbols),0);
        else {
            pos=strchr(shifted+1,*text); CHECK(pos);
            key(studio,(tic_key)(pos-shifted),tic_key_shift);
        }
    }
}
static void command(Studio *studio,const char *text)
{
    CHECK(getStudioMode(studio)==TIC_CONSOLE_MODE);
    type(studio,text); key(studio,tic_key_return,0); idle(studio,30);
}
static Studio *create(const char *folder)
{
    char *args[]={"tic80-studio","--skip",NULL};
    Studio *studio=studio_create(2,args,48000,TIC80_PIXEL_COLOR_RGBA8888,folder,1,tic_layout_qwerty);
    CHECK(studio); tm_studio_bind(studio);
    studio_config_get(studio)->data.checkNewVersion=false;
    idle(studio,120); CHECK(getStudioMode(studio)==TIC_CONSOLE_MODE);
    return studio;
}
static void capture(Studio *studio,const char *name)
{
    if(!screens) return;
    char path[1024]; CHECK(snprintf(path,sizeof path,"%s/%s.png",screens,name)<(int)sizeof path);
    png_img img={.width=TIC80_FULLWIDTH,.height=TIC80_FULLHEIGHT,
        .values=(u32 *)studio_mem(studio)->product.screen};
    png_buffer png=png_write(img,(png_buffer){0}); CHECK(png.data&&png.size>0);
    FILE *file=fopen(path,"wb"); CHECK(file);
    CHECK(fwrite(png.data,1,png.size,file)==(size_t)png.size); CHECK(fclose(file)==0);
    free(png.data);
}
static void clear_code(Studio *studio)
{
    key(studio,tic_key_a,tic_key_ctrl); key(studio,tic_key_backspace,0);
    CHECK(getMemory(studio)->cart.code.data[0]==0);
}
static void click(Studio *studio,unsigned x,unsigned y)
{
    tic80_input input={0};
    input.mouse.x=x+TIC80_OFFSET_LEFT; input.mouse.y=y+TIC80_OFFSET_TOP;
    tick(studio,input); input.mouse.left=true; tick(studio,input);
    input.mouse.left=false; tick(studio,input);
}
static int cleanup(const char *path,const struct stat *st,int kind,struct FTW *walk)
{ (void)st; (void)kind; (void)walk; return remove(path); }

int main(int argc,char **argv)
{
    CHECK(argc<=2); screens=argc==2?argv[1]:NULL;
    char folder[]="/tmp/tic80-studio-XXXXXX"; CHECK(mkdtemp(folder));
    tm_studio_clock(0);
    Studio *studio=create(folder); capture(studio,"console");
    tm_studio_sprite_view before[8], candidate[8], after[8];
    tm_studio_sprite_views(studio,before); memcpy(candidate,before,sizeof before);
    candidate[0].color^=1; // a valid early record must remain unapplied
    candidate[7].bpp=0; // invalid final record would divide by zero if restored
    CHECK(!tm_studio_restore_sprite_views(studio,candidate));
    tm_studio_sprite_views(studio,after); CHECK(!memcmp(before,after,sizeof before));
    tm_studio_code_view original, bad, observed;
    tm_studio_code_state(studio,&original);
    size_t length=strlen(getMemory(studio)->cart.code.data);
    for(unsigned i=0;i<27;++i) {
        bad=original;
        switch(i) {
            case 0: bad.cursor=length+1; break;
            case 1: bad.selection=length+1; break;
            case 2: bad.selection=-2; break;
            case 3: bad.column=-1; break;
            case 4: bad.scroll_y=-1; break;
            case 5: bad.flags=1u<<31; break;
            case 6: bad.vi_mode=VI_SEEK_BACK+1; break;
            case 7: bad.vi_mode=UINT32_MAX; break;
            case 8: bad.mode=TM_CODE_EDIT+1; break;
            case 9: bad.animation=TM_CODE_HIDE+1; break;
            case 10: bad.animation_tick=STUDIO_ANIM_TIME+1; break;
            case 11: bad.animation_tick=1; break; // idle has no persisted clock
            case 12: bad.popup_y=-TOOLBAR_SIZE-1; break;
            case 13: bad.sidebar_x=12*TIC_FONT_WIDTH+1; break;
            case 14: bad.previous_cursor=length+1; break;
            case 15: bad.previous_selection=length+1; break;
            case 16: bad.previous_cursor=-1; bad.previous_selection=0; break;
            case 17: bad.replace_offset=TM_CODE_POPUP_BYTES; break;
            case 18: bad.replace_offset=0; break;
            case 19: memset(bad.popup_text,'a',sizeof bad.popup_text); break;
            case 20: bad.reserved[1]=1; break;
            case 21: bad.jump_line=-2; break;
            case 22: bad.sidebar_count=TIC_CODE_SIZE; break;
            case 23: bad.sidebar_index=1; break;
            case 24: bad.sidebar_scroll=1; break;
            case 25: bad.mode=TM_CODE_BOOKMARK; bad.sidebar_count=1; bad.flags^=TM_CODE_ALT_FONT; break;
            case 26: bad.mode=TM_CODE_GOTO; bad.jump_line=TIC_CODE_SIZE-1; bad.flags^=TM_CODE_SHADOW; break;
        }
        CHECK(!tm_studio_restore_code(studio,&bad));
        tm_studio_code_state(studio,&observed);
        CHECK(!memcmp(&observed,&original,sizeof original));
    }
    bad=original; bad.cursor=length; bad.selection=0;
    bad.column=17; bad.scroll_x=3; bad.scroll_y=2;
    bad.flags^=TM_CODE_ALT_FONT|TM_CODE_SHADOW;
    CHECK(tm_studio_restore_code(studio,&bad));
    tm_studio_code_state(studio,&observed); CHECK(!memcmp(&bad,&observed,sizeof bad));
    CHECK(tm_studio_restore_code(studio,&original));
    bad=original; bad.mode=TM_CODE_REPLACE; strcpy(bad.popup_text,"a WITH:b"); bad.replace_offset=1;
    bad.previous_cursor=0; bad.previous_selection=length;
    CHECK(tm_studio_restore_code(studio,&bad));
    tm_studio_code_state(studio,&observed); CHECK(!memcmp(&bad,&observed,sizeof bad));
    tm_studio_code_view replace=bad;
    bad.replace_offset=2;
    CHECK(!tm_studio_restore_code(studio,&bad)); tm_studio_code_state(studio,&observed); CHECK(!memcmp(&replace,&observed,sizeof replace));
    CHECK(tm_studio_restore_code(studio,&original));
    puts("Studio code-view validation: malformed offsets, labels, modes, animation, sidebar and goto rejected atomically; replacement pointer rebound");
    for(unsigned mode=VI_NORMAL;mode<=VI_SEEK_BACK;++mode) {
        bad=original; bad.vi_mode=mode;
        CHECK(tm_studio_restore_code(studio,&bad));
        tm_studio_code_state(studio,&observed); CHECK(!memcmp(&bad,&observed,sizeof bad));
    }
    CHECK(tm_studio_restore_code(studio,&original));
    command(studio,"new lua"); CHECK(strstr(getMemory(studio)->cart.code.data,"function TIC()"));
    key(studio,tic_key_f1,0); CHECK(getStudioMode(studio)==TIC_CODE_MODE);
    clear_code(studio);
    type(studio,"aB");
    key(studio,tic_key_capslock,0); type(studio,"c");
    key(studio,tic_key_d,tic_key_shift);
    type(studio,"1"); key(studio,tic_key_1,tic_key_shift);
    tic80_input caps={0}; caps.keyboard.keys[0]=tic_key_capslock;
    for(unsigned i=0;i<35;++i) tick(studio,caps);
    idle(studio,1); type(studio,"e");
    for(tic_key k=tic_key_numpad0;k<=tic_key_numpad9;++k) key(studio,k,0);
    key(studio,tic_key_numpadplus,0); key(studio,tic_key_numpadminus,0);
    key(studio,tic_key_numpadmultiply,0); key(studio,tic_key_numpaddivide,0);
    key(studio,tic_key_numpadperiod,0);
    CHECK(strcmp(getMemory(studio)->cart.code.data,"aBCd1!e0123456789+-*/.")==0);
    key(studio,tic_key_z,tic_key_ctrl);
    CHECK(strcmp(getMemory(studio)->cart.code.data,"aBCd1!e0123456789+-*/")==0);
    key(studio,tic_key_y,tic_key_ctrl);
    CHECK(strcmp(getMemory(studio)->cart.code.data,"aBCd1!e0123456789+-*/.")==0);
    clear_code(studio);
    tic80_input held={0}; held.keyboard.keys[0]=tic_key_f;
    for(unsigned i=0;i<35;++i) tick(studio,held);
    idle(studio,1);
    size_t repeats=strlen(getMemory(studio)->cart.code.data); CHECK(repeats>1&&repeats<12);
    CHECK(strspn(getMemory(studio)->cart.code.data,"f")==repeats);
    clear_code(studio);

    const char code[]="-- title: Studio regression\nfunction TIC()\n cls(6)\nend\n";
    tic_sys_clipboard_set(code); key(studio,tic_key_v,tic_key_ctrl);
    CHECK(strcmp(getMemory(studio)->cart.code.data,code)==0);
    key(studio,tic_key_a,tic_key_ctrl); key(studio,tic_key_c,tic_key_ctrl);
    char *copied=tic_sys_clipboard_get(); CHECK(copied&&strcmp(copied,code)==0);
    tic_sys_clipboard_free(copied);
    key(studio,tic_key_x,tic_key_ctrl); CHECK(getMemory(studio)->cart.code.data[0]==0);
    key(studio,tic_key_v,tic_key_ctrl); CHECK(strcmp(getMemory(studio)->cart.code.data,code)==0);
    capture(studio,"code");

    const EditorMode modes[]={TIC_SPRITE_MODE,TIC_MAP_MODE,TIC_SFX_MODE,TIC_MUSIC_MODE};
    const char *names[]={"sprite","map","sfx","music"};
    for(unsigned i=0;i<4;++i) {
        key(studio,(tic_key)(tic_key_f2+i),0); CHECK(getStudioMode(studio)==modes[i]);
        if(i==0) {
            Sprite *sprite=getSpriteEditor(studio);
            CHECK(sprite->x==1&&sprite->y==0);
            u8 before=getSpritePixel(sprite->src->data,8,0);
            click(studio,24+3*8+4,112+4); CHECK(sprite->color==3);
            click(studio,24+4,20+4); CHECK(getSpritePixel(sprite->src->data,8,0)==3);
            key(studio,tic_key_z,tic_key_ctrl); CHECK(getSpritePixel(sprite->src->data,8,0)==before);
            key(studio,tic_key_y,tic_key_ctrl); CHECK(getSpritePixel(sprite->src->data,8,0)==3);
            tic_tiles *bank0=sprite->src;
            key(studio,tic_key_7,tic_key_ctrl);
            sprite=getSpriteEditor(studio); CHECK(sprite->src!=bank0);
            click(studio,24+5*8+4,112+4); CHECK(sprite->color==5);
            click(studio,24+4,20+4); CHECK(getSpritePixel(sprite->src->data,8,0)==5);
            key(studio,tic_key_0,tic_key_ctrl);
            CHECK(getSpriteEditor(studio)->src==bank0);
            CHECK(getSpritePixel(bank0->data,8,0)==3);
        }
        idle(studio,3); capture(studio,names[i]);
    }
    setStudioMode(studio,TIC_WORLD_MODE); idle(studio,3); capture(studio,"world");
    getMemory(studio)->cart.banks[7].map.data[3]=173;
    key(studio,tic_key_escape,0); CHECK(getStudioMode(studio)==TIC_CONSOLE_MODE);
    command(studio,"save fixture.tic");
    command(studio,"save fixture.png");
    command(studio,"save fixture.lua");
    key(studio,tic_key_r,tic_key_ctrl); CHECK(getStudioMode(studio)==TIC_RUN_MODE);
    idle(studio,5); CHECK(tic_api_pix(getMemory(studio),0,0,0,true)==6);
    capture(studio,"running"); key(studio,tic_key_escape,0);
    CHECK(getStudioMode(studio)==TIC_CONSOLE_MODE);
    tm_studio_bind(NULL); studio_delete(studio);

    const char *loads[]={"load fixture.tic","load fixture.png","load fixture.lua"};
    for(unsigned i=0;i<3;++i) {
        studio=create(folder); command(studio,loads[i]);
        CHECK(strcmp(getMemory(studio)->cart.code.data,code)==0);
        CHECK(getMemory(studio)->cart.banks[7].map.data[3]==173);
        CHECK(getSpritePixel(getSpriteEditor(studio)->src->data,8,0)==3);
        key(studio,tic_key_f2,0); key(studio,tic_key_7,tic_key_ctrl);
        CHECK(getSpritePixel(getSpriteEditor(studio)->src->data,8,0)==5);
        key(studio,tic_key_0,tic_key_ctrl); key(studio,tic_key_escape,0);
        key(studio,tic_key_r,tic_key_ctrl); idle(studio,3);
        CHECK(getStudioMode(studio)==TIC_RUN_MODE);
        CHECK(tic_api_pix(getMemory(studio),0,0,0,true)==6);
        key(studio,tic_key_escape,0); CHECK(getStudioMode(studio)==TIC_CONSOLE_MODE);
        if(i==2) { command(studio,"surf"); CHECK(getStudioMode(studio)==TIC_SURF_MODE); capture(studio,"surf"); }
        tm_studio_bind(NULL); studio_delete(studio);
    }
    CHECK(nftw(folder,cleanup,16,FTW_DEPTH|FTW_PHYS)==0);
    printf("Studio console commands, text/Caps Lock/keypad/repeat, undo/redo, clipboard, mouse sprite editing in banks 0/7, six editor views, native/PNG/project save-reload and run/escape passed (%llu ticks, 800 stereo frames per tick)\n",(unsigned long long)frames);
    return 0;
}
