#include "Vtic80_horizontal_wheel.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
int main()
{
    Vtic80_horizontal_wheel d;
    d.clk=0; d.reset=1; d.osd_open=0; d.io_enable=0; d.io_strobe=0; d.io_din=0;
    auto tick=[&] { d.clk=0; d.eval(); d.clk=1; d.eval(); };
    auto word=[&](unsigned w) { d.io_enable=1; d.io_din=w; d.io_strobe=1; tick(); d.io_strobe=0; for(int i=0;i<7;++i) tick(); };
    auto end=[&] { d.io_enable=0; for(int i=0;i<8;++i) tick(); };
    auto packet=[&](int32_t delta) { word(0x45); word((uint32_t)delta & 65535); word((uint32_t)delta >> 16); end(); };
    tick(); d.reset=0; tick(); CHECK(d.total==0);
    packet(3); CHECK(d.total==3); packet(-7); CHECK(d.total==0xfffffffc);
    packet(10); CHECK(d.total==6);
    // Unknown, truncated, oversized and saturated-length packets do not commit.
    word(0x04); word(120); word(0); end(); CHECK(d.total==6);
    word(0x45); end(); CHECK(d.total==6);
    word(0x45); word(80); end(); CHECK(d.total==6);
    word(0x45); word(9); word(0); word(0); end(); CHECK(d.total==6);
    word(0x45); for(unsigned i=0;i<260;++i) word(1); end(); CHECK(d.total==6);
    packet(INT32_MIN); CHECK(d.total==0x80000006); packet(INT32_MIN); CHECK(d.total==6);
    // OSD opening at any point invalidates the transaction, including if it
    // closes before the packet finishes. Holding enable low cannot replay it.
    d.osd_open=1; for(int i=0;i<4;++i) tick(); packet(50); CHECK(d.total==6);
    d.osd_open=0; for(int i=0;i<4;++i) tick();
    word(0x45); word(30); d.osd_open=1; for(int i=0;i<4;++i) tick();
    d.osd_open=0; for(int i=0;i<4;++i) tick(); word(0); end(); CHECK(d.total==6);
    packet(-6); CHECK(d.total==0);
    word(0x45); word(13); d.reset=1; tick(); d.reset=0; word(0); end(); CHECK(d.total==0);
    packet(1); CHECK(d.total==1);
    puts("Horizontal UIO: signed/wrapping totals, exact transactions, no replay, OSD suppression and reset passed");
}
