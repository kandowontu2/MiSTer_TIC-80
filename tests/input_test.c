#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/input.h"
#include "tic.h"
#include "cart.h"
#include "api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void held(tm_input_snapshot *s, unsigned code, int down)
{
    if(down) s->keys[code/32] |= 1u<<(code%32);
    else s->keys[code/32] &= ~(1u<<(code%32));
}
static int errors;
static void error(const char *message) { fprintf(stderr,"%s\n",message); ++errors; }
static u64 tick;
static u64 counter(void *data) { (void)data; return tick; }
static u64 frequency(void *data) { (void)data; return 60; }
int main(void)
{
    tm_input_state state={0};
    tm_input_snapshot s={0};
    tic80_input input={0};
    unsigned seen[tic_keys_count]={0};
    for(unsigned code=0;code<512;++code) {
        memset(s.keys,0,sizeof s.keys);held(&s,code,1);
        tm_input_convert(&state,&s,&input);
        assert(input.keyboard.keys[0]<tic_keys_count);
        seen[input.keyboard.keys[0]]++;
        assert(input.keyboard.keys[1]==0);
    }
    for(unsigned key=1;key<tic_keys_count;++key) assert(seen[key]);
    memset(s.keys,0,sizeof s.keys);
    held(&s,0x14,1);held(&s,0x114,1);held(&s,0x12,1);held(&s,0x59,1);held(&s,0x1c,1);
    tm_input_convert(&state,&s,&input);
    assert(input.keyboard.keys[0]==tic_key_ctrl && input.keyboard.keys[1]==tic_key_shift && input.keyboard.keys[2]==tic_key_a && input.keyboard.keys[3]==0);
    held(&s,0x14,0);held(&s,0x12,0);tm_input_convert(&state,&s,&input);
    assert(input.keyboard.keys[0]==tic_key_ctrl && input.keyboard.keys[1]==tic_key_shift);
    held(&s,0x11,1);held(&s,0x32,1);held(&s,0x21,1);tm_input_convert(&state,&s,&input);
    assert(input.keyboard.keys[2]==tic_key_alt && input.keyboard.keys[3]==tic_key_a);
    memset(s.keys,0,sizeof s.keys);
    held(&s,0x175,1);held(&s,0x172,1);held(&s,0x16b,1);held(&s,0x174,1);
    held(&s,0x1a,1);held(&s,0x22,1);held(&s,0x1c,1);held(&s,0x1b,1);
    input.gamepads.data=0x80402000;
    tm_input_convert(&state,&s,&input);
    assert(input.gamepads.data==0x804020ff); // keyboard does not lose other pads
    memset(s.keys,0,sizeof s.keys);
    static const tic_key mapped[] = {tic_key_w,tic_key_a,tic_key_s,tic_key_d,tic_key_return,tic_key_escape,tic_key_q,tic_key_e};
    for(unsigned pad=0;pad<4;++pad) for(unsigned action=0;action<8;++action) {
        memset(s.joystick,0,sizeof s.joystick);
        s.joystick[pad]=1u<<(8+action); input.gamepads.data=0x80402001;
        tm_input_convert(&state,&s,&input);
        assert(input.keyboard.keys[0]==mapped[action] && !input.keyboard.keys[1]);
        assert(input.gamepads.data==0x80402001);
    }
    memset(s.joystick,0,sizeof s.joystick);
    input.gamepads.data=0; tm_input_convert(&state,&s,&input);
    assert(!input.keyboard.data && !input.gamepads.data);
    // Independent stick and D-pad, including diagonal WASD and held modifier.
    s.joystick[0]=(1u<<8)|(1u<<9); held(&s,0x12,1);
    input.gamepads.data=8; tm_input_convert(&state,&s,&input);
    assert(input.keyboard.keys[0]==tic_key_shift && input.keyboard.keys[1]==tic_key_a && input.keyboard.keys[2]==tic_key_w);
    assert(input.gamepads.data==8);
    memset(s.keys,0,sizeof s.keys); memset(s.joystick,0,sizeof s.joystick);
    for(unsigned buttons=0;buttons<8;++buttons) {
        s.mouse=239|135*256|buttons*65536; tm_input_convert(&state,&s,&input);
        assert(input.mouse.x==239+TIC80_OFFSET_LEFT && input.mouse.y==135+TIC80_OFFSET_TOP);
        assert(input.mouse.left==!!(buttons&1) && input.mouse.right==!!(buttons&2) && input.mouse.middle==!!(buttons&4));
        assert(!input.mouse.relative && !input.mouse.scrollx);
    }
    state=(tm_input_state){0};s.wheel=0xfffffffe;tm_input_convert(&state,&s,&input);
    s.wheel=2;tm_input_convert(&state,&s,&input);assert(input.mouse.scrolly==-4);
    tm_input_convert(&state,&s,&input);assert(!input.mouse.scrolly);
    s.wheel+=63;tm_input_convert(&state,&s,&input);assert(input.mouse.scrolly==-32);
    tm_input_convert(&state,&s,&input);assert(input.mouse.scrolly==-31);
    tm_input_convert(&state,&s,&input);assert(input.mouse.scrolly==0);
    s.wheel-=65;tm_input_convert(&state,&s,&input);assert(input.mouse.scrolly==31);
    tm_input_convert(&state,&s,&input);assert(input.mouse.scrolly==31);
    tm_input_convert(&state,&s,&input);assert(input.mouse.scrolly==3);
    state=(tm_input_state){0}; s.horizontal_wheel=0xfffffffe;
    tm_input_convert(&state,&s,&input); assert(!input.mouse.scrollx);
    s.horizontal_wheel=2; tm_input_convert(&state,&s,&input); assert(input.mouse.scrollx==4);
    tm_input_convert(&state,&s,&input); assert(!input.mouse.scrollx);
    s.horizontal_wheel+=65;
    tm_input_convert(&state,&s,&input); assert(input.mouse.scrollx==31);
    tm_input_convert(&state,&s,&input); assert(input.mouse.scrollx==31);
    tm_input_convert(&state,&s,&input); assert(input.mouse.scrollx==3);
    s.horizontal_wheel-=63;
    tm_input_convert(&state,&s,&input); assert(input.mouse.scrollx==-31);
    tm_input_convert(&state,&s,&input); assert(input.mouse.scrollx==-31);
    tm_input_convert(&state,&s,&input); assert(input.mouse.scrollx==-1);
    tm_input_convert(&state,&s,&input); assert(!input.mouse.scrollx);
    static tic_cartridge cart;
    strcpy(cart.code.data,"-- script: lua\nfunction TIC() if key(1) then pmem(0,pmem(0)+1) end if keyp(1) then pmem(1,pmem(1)+1) end local x,y,l,m,r,sx,sy=mouse(); pmem(2,x); pmem(3,y); pmem(4,(l and 1 or 0)+(m and 2 or 0)+(r and 4 or 0)); pmem(5,sy+32); pmem(6,sx+32) end\n");
    u8 *bytes=malloc(sizeof cart*2);assert(bytes);
    s32 size=tic_cart_save(&cart,bytes);assert(size>0);
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888);assert(tic);
    tic->callback.error=error;tic80_load(tic,bytes,size);free(bytes);
    memset(s.keys,0,sizeof s.keys);held(&s,0x1c,1);s.mouse=37|81*256|5*65536;
    s.wheel-=7; s.horizontal_wheel-=9; tm_input_convert_player(&state,&s,&input);
    tic80_tick(tic,input,counter,frequency);assert(!errors);
    assert(tic_api_pmem((tic_mem*)tic,0,0,0)==1 && tic_api_pmem((tic_mem*)tic,1,0,0)==1);
    assert(tic_api_pmem((tic_mem*)tic,2,0,0)==37 && tic_api_pmem((tic_mem*)tic,3,0,0)==81);
    assert(tic_api_pmem((tic_mem*)tic,4,0,0)==3 && tic_api_pmem((tic_mem*)tic,5,0,0)==39);
    assert(tic_api_pmem((tic_mem*)tic,6,0,0)==41);
    tick++;tm_input_convert(&state,&s,&input);tic80_tick(tic,input,counter,frequency);
    assert(tic_api_pmem((tic_mem*)tic,0,0,0)==2 && tic_api_pmem((tic_mem*)tic,1,0,0)==1);
    assert(tic_api_pmem((tic_mem*)tic,5,0,0)==32);
    assert(tic_api_pmem((tic_mem*)tic,6,0,0)==32);
    tic80_delete(tic);
    memset(&cart,0,sizeof cart);
    snprintf(cart.code.data,sizeof cart.code.data,
        "-- script: lua\nlocal keys={%d,%d,%d,%d,%d,%d,%d,%d}\nfunction TIC() "
        "for i,k in ipairs(keys) do if key(k) then pmem(i-1,pmem(i-1)+1) end "
        "if keyp(k) then pmem(i+7,pmem(i+7)+1) end end pmem(16,btn()) end\n",
        tic_key_w,tic_key_a,tic_key_s,tic_key_d,tic_key_return,tic_key_escape,tic_key_q,tic_key_e);
    bytes=malloc(sizeof cart*2); assert(bytes);
    size=tic_cart_save(&cart,bytes); assert(size>0);
    tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); assert(tic);
    tic->callback.error=error; tic80_load(tic,bytes,size); free(bytes);
    memset(&s,0,sizeof s);
    for(unsigned action=0;action<8;++action) {
        s.joystick[action%4]=1u<<(8+action);
        for(unsigned repeat=0;repeat<2;++repeat) {
            input.gamepads.data=8; tm_input_convert(&state,&s,&input);
            tick++; tic80_tick(tic,input,counter,frequency); assert(!errors);
        }
        memset(s.joystick,0,sizeof s.joystick);
        input.gamepads.data=8; tm_input_convert(&state,&s,&input);
        tick++; tic80_tick(tic,input,counter,frequency);
        assert(tic_api_pmem((tic_mem*)tic,action,0,0)==2);
        assert(tic_api_pmem((tic_mem*)tic,action+8,0,0)==1);
        assert(tic_api_pmem((tic_mem*)tic,16,0,0)==8);
    }
    tic80_delete(tic);
    puts("Runtime input: all TIC keys, modifiers, controller WASD/Enter/Esc/Q/E key/keyp and release, independent D-pad, mouse/wheel passed");
    return 0;
}
