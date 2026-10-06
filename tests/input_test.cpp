#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Vtic80_input.h"
#include <cassert>
#include <cstdio>
int main()
{
    Vtic80_input d;
    d.clk=0; d.reset=1; d.osd_open=0; d.ps2_key=0; d.ps2_mouse=0; d.ps2_mouse_ext=0;
    auto tick=[&] { d.clk=0; d.eval(); d.clk=1; d.eval(); };
    auto key=[&](unsigned code, bool down) {
        d.ps2_key=((d.ps2_key^1024)&1024)|(down?512:0)|code; tick(); tick();
    };
    auto held=[&](unsigned code) { return (d.keys[code/32]>>(code%32))&1; };
    auto mouse=[&](int x,int y,int wheel,unsigned buttons,bool overflow=false) {
        unsigned flags=8|buttons|(x<0?16:0)|(y<0?32:0)|(overflow?192:0);
        d.ps2_mouse=((d.ps2_mouse^(1<<24))&(1<<24))|flags|((x&255)<<8)|((y&255)<<16);
        d.ps2_mouse_ext=wheel&255; tick(); tick();
    };
    tick(); d.reset=0; tick();
    assert((d.mouse&65535)==(68*256+120));
    for (unsigned code=0;code<512;++code) {
        key(code,true); assert(held(code));
        for(unsigned other=0;other<512;++other) assert(held(other)==(other==code));
        key(code,false); assert(!held(code));
    }
    key(0x14,true);key(0x114,true);key(0x14,false);assert(held(0x114));
    key(0x12,true);key(0x59,true);key(0x12,false);assert(held(0x59));
    mouse(10,5,3,7); assert((d.mouse&0xffffff)==(7*65536+63*256+130));
    assert((d.mouse>>32)==3);
    mouse(-200,-200,-4,0);assert((d.mouse&65535)==135*256);
    assert((d.mouse>>32)==0xffffffffULL);
    mouse(255,255,1,1,true);assert((d.mouse&65535)==239);
    assert((d.mouse>>32)==0);
    d.osd_open=1;tick();tick();tick();
    for(unsigned code=0;code<512;++code) assert(!held(code));
    assert(((d.mouse>>16)&7)==0);
    key(0x1c,true);mouse(-20,-10,5,7);
    assert(!held(0x1c) && (d.mouse>>32)==0 && (d.mouse&65535)==239);
    d.osd_open=0;tick();tick();tick();assert(!held(0x1c));
    key(0x1c,false);key(0x1c,true);assert(held(0x1c));
    puts("FPGA input: all 512 physical keys, modifier sides, pointer bounds, signed wheel, OSD clearing passed");
}
