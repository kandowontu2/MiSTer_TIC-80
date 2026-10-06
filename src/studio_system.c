#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/studio.h"
#include "studio/studio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static Studio *active;
static char *clipboard;
static uint64_t clock_value;
static bool fixed_clock;
static bool caps_lock, caps_down;
static tm_studio_pmem_loader pmem_loader;
static void *pmem_data;
static bool pmem_failed;
void tm_studio_pmem_hook(tm_studio_pmem_loader loader,void *data)
{ pmem_loader=loader; pmem_data=data; pmem_failed=false; }
bool tm_studio_pmem_managed(void) { return pmem_loader!=NULL; }
bool tm_studio_pmem_failed(void) { return pmem_failed; }
int tm_studio_pmem_load(Studio *studio)
{ int result=pmem_loader?pmem_loader(studio,pmem_data):0; pmem_failed=result<0; return result; }
bool tm_studio_code_view_valid(const tm_studio_code_view *v,size_t length)
{
    if(!(v && length<TIC_CODE_SIZE && v->cursor>=0 && (size_t)v->cursor<=length &&
        v->selection>=-1 && (v->selection<0 || (size_t)v->selection<=length) &&
        v->column>=0 && v->column<TIC_CODE_SIZE &&
        v->scroll_x>=0 && v->scroll_x<TIC_CODE_SIZE &&
        v->scroll_y>=0 && v->scroll_y<TIC_CODE_SIZE &&
        !(v->flags&~(TM_CODE_ALT_FONT|TM_CODE_SHADOW)) && v->vi_mode<=VI_SEEK_BACK &&
        v->mode<=TM_CODE_EDIT && v->animation<=TM_CODE_HIDE &&
        v->animation_tick>=0 && v->animation_tick<=STUDIO_ANIM_TIME &&
        (v->animation!=TM_CODE_IDLE || !v->animation_tick) &&
        v->popup_y>=-TOOLBAR_SIZE && v->popup_y<=0 && v->sidebar_x>=0 && v->sidebar_x<=12*TIC_FONT_WIDTH &&
        v->previous_cursor>=-1 && (v->previous_cursor<0 || (size_t)v->previous_cursor<=length) &&
        v->previous_selection>=-1 && (v->previous_selection<0 || (size_t)v->previous_selection<=length) &&
        (v->previous_cursor>=0 || v->previous_selection==-1) &&
        v->replace_offset>=-1 && v->replace_offset<TM_CODE_POPUP_BYTES &&
        v->jump_line>=-1 && v->jump_line<TIC_CODE_SIZE &&
        v->sidebar_count>=0 && v->sidebar_count<TIC_CODE_SIZE &&
        v->sidebar_index>=0 && (v->sidebar_count ? v->sidebar_index<v->sidebar_count : !v->sidebar_index) &&
        v->sidebar_scroll>=0 && v->sidebar_scroll<=v->sidebar_count &&
        memchr(v->popup_text,0,sizeof v->popup_text) && !v->reserved[0] && !v->reserved[1])) return false;
    if(v->replace_offset>=0) {
        size_t used=strlen(v->popup_text);
        if(v->mode!=TM_CODE_REPLACE || !v->replace_offset ||
           (size_t)v->replace_offset+sizeof " WITH:"-1>used ||
           memcmp(v->popup_text+v->replace_offset," WITH:",sizeof " WITH:"-1)) return false;
    }
    return true;
}
bool tm_studio_sprite_view_valid(const tm_studio_sprite_view *v)
{
    if(!v || (v->bpp!=1 && v->bpp!=2 && v->bpp!=4) ||
       (v->size!=8 && v->size!=16 && v->size!=32 && v->size!=64)) return false;
    return v->x<=(128-v->size)/8 && v->y<=(128-v->size)/8 &&
        v->brush_size>=1 && v->brush_size<=4 && v->color<(1u<<v->bpp) &&
        v->color2<(1u<<v->bpp) && v->bank<2 && v->page<4/v->bpp &&
        v->tool<=3 && !(v->flags&~(TM_SPRITE_ADVANCED|TM_SPRITE_HEX_INDEX|TM_SPRITE_PALETTE_BANK1));
}
void tm_studio_caps_state(bool *latched,bool *down) { *latched=caps_lock; *down=caps_down; }
void tm_studio_restore_caps(bool latched,bool down) { caps_lock=latched; caps_down=down; }
void tm_studio_bind(Studio *studio)
{
    if(active!=studio) {
        caps_lock=caps_down=false;
        free(clipboard); clipboard=NULL;
    }
    active=studio;
}
void tm_studio_tick(Studio *studio, tic80_input input)
{
    if(active!=studio) tm_studio_bind(studio);
    bool down=false;
    for(unsigned i=0;i<TIC80_KEY_BUFFER;++i)
        if(input.keyboard.keys[i]==tic_key_capslock) down=true;
    if(down&&!caps_down) caps_lock=!caps_lock;
    caps_down=down;
    studio_tick(studio,input);
}
void tm_studio_clock(uint64_t value) { fixed_clock=true; clock_value=value; }
void tm_studio_real_clock(void) { fixed_clock=false; }
void tic_sys_clipboard_set(const char *text)
{
    char *next=strdup(text?text:"");
    if(next) { free(clipboard); clipboard=next; }
}
bool tic_sys_clipboard_has(void) { return clipboard && *clipboard; }
char *tic_sys_clipboard_get(void) { return strdup(clipboard?clipboard:""); }
void tic_sys_clipboard_free(const char *text) { free((void *)text); }
u64 tic_sys_counter_get(void)
{
    if(fixed_clock) return clock_value;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (u64)now.tv_sec*1000000000ULL+now.tv_nsec;
}
u64 tic_sys_freq_get(void) { return 1000000000ULL; }
bool tic_sys_fullscreen_get(void) { return true; }
void tic_sys_fullscreen_set(bool value) { (void)value; } /* MiSTer always uses the full output. */
void tic_sys_message(const char *title,const char *message)
{
    fprintf(stderr,"%s: %s\n",title,message);
    if(active) tm_studio_popup(active,message);
}
void tic_sys_title(const char *title) { (void)title; }
void tic_sys_open_path(const char *path)
{
    tic_sys_clipboard_set(path);
    tic_sys_message("TIC-80","Path copied to the editor clipboard");
}
void tic_sys_open_url(const char *url)
{
    tic_sys_clipboard_set(url);
    tic_sys_message("TIC-80","URL copied to the editor clipboard");
}
void tic_sys_preseed(void) { srand((unsigned)tic_sys_counter_get()); }
bool tic_sys_keyboard_text(char *text)
{
    *text='\0';
    if(!active) return true;
    tic_mem *tic=getMemory(active);
    if(tic_api_key(tic,tic_key_ctrl)||tic_api_key(tic,tic_key_alt)) return true;
    const bool shift=tic_api_key(tic,tic_key_shift);
    const char symbols[]=" abcdefghijklmnopqrstuvwxyz0123456789-=[]\\;'`,./ ";
    const char shifted[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ)!@#$%^&*(_+{}|:\"~<>? ";
    for(unsigned i=0;i<TIC80_KEY_BUFFER;++i) {
        tic_key key=tic->ram->input.keyboard.keys[i];
        char ch=0;
        if(key>tic_key_unknown&&key<=tic_key_space) {
            bool upper=shift;
            if(key>=tic_key_a&&key<=tic_key_z) upper^=caps_lock;
            ch=upper?shifted[key]:symbols[key];
        } else if(key>=tic_key_numpad0&&key<=tic_key_numpad9)
            ch='0'+key-tic_key_numpad0;
        else switch(key) {
            case tic_key_numpadplus: ch='+'; break;
            case tic_key_numpadminus: ch='-'; break;
            case tic_key_numpadmultiply: ch='*'; break;
            case tic_key_numpaddivide: ch='/'; break;
            case tic_key_numpadperiod: ch='.'; break;
            default: break;
        }
        if(ch&&tic_api_keyp(tic,key,KEYBOARD_HOLD,KEYBOARD_PERIOD)) {
            *text=ch; break;
        }
    }
    return true;
}
void tic_sys_update_config(void) {}
void tic_sys_default_mapping(tic_mapping *mapping)
{
    memset(mapping,0,sizeof *mapping);
    const tic_key first[]={tic_key_up,tic_key_down,tic_key_left,tic_key_right,tic_key_z,tic_key_x,tic_key_a,tic_key_s};
    for(unsigned i=0;i<sizeof first/sizeof *first;++i) mapping->data[i]=first[i];
}
