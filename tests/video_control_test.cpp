#include "Vtic80_video_control.h"
#include "tic80_mister/exchange.h"
#include "tic80_mister/memory_map.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static void step(Vtic80_video_control &dut)
{
    dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval();
}
int main()
{
    Vtic80_video_control dut;
    dut.reset = 1; dut.reads_idle = 1; dut.vblank_start = 0;
    dut.video_publish = 0; dut.session_request = 0;
    step(dut); dut.reset = 0;
    CHECK(dut.identity == TM_MAGIC);
    CHECK(dut.geometry == (TM_HEIGHT << 16 | TM_WIDTH));
    CHECK(!dut.front_valid);
    /* Stale publication cannot display before a session is established. */
    dut.video_publish = 0xFFFF0002; dut.vblank_start = 1; step(dut);
    CHECK(!dut.front_valid && dut.heartbeat == 1);
    dut.video_publish = 0; dut.vblank_start = 0;
    dut.session_request = 37; step(dut);
    CHECK(dut.session_ack == 37 && dut.session_reset);
    tm_exchange exchange;
    tm_exchange_init(&exchange);
    unsigned buffer;
    uint32_t publication;
    for (unsigned frame = 0; frame < 10000; ++frame) {
        dut.vblank_start = 0;
        CHECK(tm_exchange_acquire(&exchange, dut.video_presented, &buffer) == 1);
        if (dut.front_valid) CHECK(buffer != (dut.video_presented & 1));
        CHECK(tm_exchange_publish(&exchange, &publication) == 0);
        uint32_t previous = dut.video_presented;
        dut.video_publish = publication;
        for (unsigned delay = 0; delay < frame % 13 + 1; ++delay) {
            step(dut);
            CHECK(dut.video_presented == previous);
            CHECK(tm_exchange_acquire(&exchange, dut.video_presented, &buffer) == 0);
        }
        dut.vblank_start = 1;
        dut.reads_idle = 0; step(dut);
        CHECK(dut.video_presented == previous); /* Outstanding read delays adoption. */
        dut.reads_idle = 1; step(dut);
        CHECK(dut.video_presented == publication);
        CHECK(dut.front_valid);
        CHECK(dut.front_byte_address == TM_PHYSICAL_BASE +
              ((publication & 1) ? TM_BUFFER1_OFFSET : TM_BUFFER0_OFFSET));
    }
    /* Producer restart must wait for outstanding reads to end. */
    dut.vblank_start = 0; dut.reads_idle = 0;
    dut.video_publish = 0; dut.session_request = 38; step(dut);
    CHECK(dut.session_ack == 37 && !dut.session_reset);
    dut.reads_idle = 1; step(dut);
    CHECK(dut.session_ack == 38 && dut.session_reset && !dut.front_valid);
    dut.vblank_start = 1; step(dut);
    CHECK(!dut.front_valid && !dut.session_reset);
    dut.final();
    std::puts("ARM producer/RTL frame ownership co-simulation passed");
}
