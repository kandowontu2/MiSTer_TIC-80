#include "Vtic80_ddr_video.h"
#include "tic80_mister/exchange.h"
#include "tic80_mister/memory_map.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <unordered_map>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static uint32_t address(uint32_t offset) { return (TM_PHYSICAL_BASE + offset) >> 3; }
struct Bus {
    Vtic80_ddr_video dut;
    std::unordered_map<uint32_t, uint64_t> memory;
    std::array<std::array<uint64_t, TM_FRAME_BYTES / 8>, 2> frames{};
    std::deque<uint64_t> response;
    uint32_t cycle = 0, delay = 0, frame_writes = 0;
    unsigned forced_stall = 0;
    bool stalled = false;
    uint32_t stalled_address = 0;
    uint64_t stalled_data = 0;
    bool stalled_write = false;
    bool last_toggle = false;
    void step() {
        dut.consumed_toggle = dut.frame_toggle;
        dut.clk = 0;
        dut.ddr_busy = forced_stall ? true : cycle % 11 < 3;
        if (forced_stall) --forced_stall;
        dut.ddr_dout_ready = !response.empty() && !delay && cycle % 5 != 0;
        if (dut.ddr_dout_ready) dut.ddr_dout = response.front();
        dut.eval();
        if (stalled) {
            CHECK(dut.ddr_rd || dut.ddr_we);
            CHECK(dut.ddr_addr == stalled_address);
            CHECK(!!dut.ddr_we == stalled_write);
            if (stalled_write) CHECK(dut.ddr_din == stalled_data);
        }
        stalled = (dut.ddr_rd || dut.ddr_we) && dut.ddr_busy;
        if (stalled) {
            stalled_address = dut.ddr_addr; stalled_data = dut.ddr_din; stalled_write = dut.ddr_we;
        }
        CHECK(!(dut.ddr_rd && dut.ddr_we));
        if (dut.ddr_rd && !dut.ddr_busy) {
            CHECK(response.empty());
            for (unsigned i = 0; i < dut.ddr_burstcnt; ++i)
                response.push_back(memory[dut.ddr_addr + i]);
            delay = 2 + cycle % 7;
        }
        if (dut.ddr_we && !dut.ddr_busy) {
            CHECK(dut.ddr_burstcnt == 1 && dut.ddr_be == 255);
            memory[dut.ddr_addr] = dut.ddr_din;
        }
        if (dut.frame_write) {
            CHECK(dut.frame_write_address == frame_writes);
            CHECK(dut.frame_write_address < TM_FRAME_BYTES / 8);
            frames[dut.frame_write_bank][dut.frame_write_address] = dut.frame_write_data;
            ++frame_writes;
        }
        if (dut.ddr_dout_ready) response.pop_front();
        if (delay) --delay;
        dut.clk = 1; dut.eval();
        if (dut.frame_toggle != last_toggle) {
            CHECK(frame_writes == TM_FRAME_BYTES / 8);
            frame_writes = 0;
            last_toggle = dut.frame_toggle;
        }
        ++cycle;
    }
    template<class Predicate> void until(Predicate predicate) {
        unsigned limit = cycle + 200000;
        while (!predicate() && cycle < limit) step();
        CHECK(predicate());
    }
};
int main() {
    Bus bus;
    bus.dut.reset = 1; bus.dut.vblank_start = 0;
    bus.dut.joystick_0 = 0x12345678; bus.dut.joystick_1 = 0x87654321;
    bus.dut.joystick_2 = 0x01020304; bus.dut.joystick_3 = 0xFFEECCAA;
    // Old nonzero session/publication must be treated as a boot baseline.
    bus.memory[address(TM_SESSION_REQUEST_OFFSET)] = 9;
    bus.memory[address(TM_VIDEO_PUBLISH_OFFSET)] = 0xABCDE002;
    bus.step(); bus.dut.reset = 0;
    for (unsigned i = 0; i < 300; ++i) {
        bus.dut.vblank_start = i % 50 == 0; bus.step();
    }
    CHECK(!bus.dut.source_valid && bus.frame_writes == 0);
    CHECK(bus.memory[address(TM_IDENTITY_OFFSET)] == ((uint64_t)TM_CART_SOURCE_MAGIC<<32 | TM_MAGIC));
    CHECK(bus.memory[address(TM_GEOMETRY_OFFSET)] == ((uint64_t)TM_LINUX_INPUT_MAGIC<<32 | TM_HEIGHT << 16 | TM_WIDTH));
    CHECK(bus.memory[address(TM_JOY3_OFFSET)] == 0xFFEECCAA);
    for (unsigned prior_state : {0u, 1u}) {
        bus.dut.cart_meta = prior_state; bus.dut.osd_status = 1;
        bus.until([&] { return bus.memory[address(TM_STATUS_OFFSET)] == 1; });
        bus.until([&] { return bus.dut.ddr_we && bus.dut.ddr_addr == address(TM_CART_META_OFFSET) &&
                              bus.dut.ddr_din == prior_state; });
        bus.forced_stall = 100; bus.step();
        // Main finishes the selected cartridge and releases its reset while
        // the preceding metadata write is still held by the real DDR master.
        bus.dut.cart_meta = (uint64_t(21) << 32) | 6; bus.dut.osd_status = 0;
        while (bus.forced_stall) bus.step();
        bus.until([&] { return bus.dut.ddr_we && bus.dut.ddr_addr == address(TM_STATUS_OFFSET); });
        CHECK(bus.dut.ddr_din == 1);
        bus.until([&] { return bus.memory[address(TM_STATUS_OFFSET)] == 0; });
        CHECK(bus.memory[address(TM_CART_META_OFFSET)] == ((uint64_t(21) << 32) | 6));
    }
    puts("DDR cart/status handoff: stalled old metadata retains reset; release follows selected ready ticket");
    bus.dut.audio_samples=0x12345678; bus.dut.audio_underruns=0xAABBCCDD;
    bus.until([&] { return (bus.memory[address(TM_STATS_SEQUENCE_OFFSET)]>>32)==TM_STATS_MAGIC &&
                          !(bus.memory[address(TM_STATS_SEQUENCE_OFFSET)]&1) &&
                          bus.memory[address(TM_AUDIO_STATS_OFFSET)]==0xAABBCCDD12345678ULL; });
    // A stalled metadata writer must retain the counters captured when its
    // sequence became odd, even if both live counters change during publication.
    uint32_t sequence=bus.memory[address(TM_STATS_SEQUENCE_OFFSET)];
    bus.dut.audio_samples=0x12345679;
    bus.until([&] { return (uint32_t)bus.memory[address(TM_STATS_SEQUENCE_OFFSET)]==sequence+1; });
    bus.dut.audio_samples=0x87654321; bus.dut.audio_underruns=0x10203040;
    bus.until([&] { return (uint32_t)bus.memory[address(TM_STATS_SEQUENCE_OFFSET)]==sequence+2; });
    CHECK(bus.memory[address(TM_AUDIO_STATS_OFFSET)]==0xAABBCCDD12345679ULL);
    bus.until([&] { return (uint32_t)bus.memory[address(TM_STATS_SEQUENCE_OFFSET)]==sequence+4; });
    CHECK(bus.memory[address(TM_AUDIO_STATS_OFFSET)]==0x1020304087654321ULL);
    for(unsigned i=0;i<500;++i) bus.step();
    CHECK((uint32_t)bus.memory[address(TM_STATS_SEQUENCE_OFFSET)]==sequence+4);
    bus.until([&] { return bus.memory[address(TM_KEYBOARD_OFFSET)] != 0 && !(bus.memory[address(TM_KEYBOARD_OFFSET)] & 1); });
    uint32_t input_sequence = bus.memory[address(TM_KEYBOARD_OFFSET)];
    for(unsigned i=0;i<16;++i) bus.dut.input_keys[i]=0x10203040u+i;
    bus.dut.input_mouse=0x1234567800054488ULL;
    bus.dut.input_horizontal_wheel=0xfffffffe;
    bus.until([&] { return bus.memory[address(TM_KEYBOARD_OFFSET)] == input_sequence+1; });
    // Change every live field while the old snapshot is still in flight.
    for(unsigned i=0;i<16;++i) bus.dut.input_keys[i]=0xA0B0C0D0u+i;
    bus.dut.input_mouse=0xFEDCBA9800021122ULL;
    bus.dut.input_horizontal_wheel=5;
    bus.until([&] { return bus.memory[address(TM_KEYBOARD_OFFSET)] == input_sequence+2; });
    for(unsigned i=0;i<8;++i) CHECK(bus.memory[address(TM_KEYBOARD_BITS_OFFSET)+i] ==
        ((uint64_t)(0x10203041u+i*2)<<32 | (0x10203040u+i*2)));
    CHECK(bus.memory[address(TM_MOUSE_OFFSET)] == 0x1234567800054488ULL);
    CHECK(bus.memory[address(TM_HORIZONTAL_WHEEL_OFFSET)] == 0xfffffffe);
    bus.until([&] { return bus.memory[address(TM_KEYBOARD_OFFSET)] == input_sequence+4; });
    for(unsigned i=0;i<8;++i) CHECK(bus.memory[address(TM_KEYBOARD_BITS_OFFSET)+i] ==
        ((uint64_t)(0xA0B0C0D1u+i*2)<<32 | (0xA0B0C0D0u+i*2)));
    CHECK(bus.memory[address(TM_MOUSE_OFFSET)] == 0xFEDCBA9800021122ULL);
    CHECK(bus.memory[address(TM_HORIZONTAL_WHEEL_OFFSET)] == 5);
    for(unsigned i=0;i<500;++i) bus.step();
    CHECK(bus.memory[address(TM_KEYBOARD_OFFSET)] == input_sequence+4);
    // Horizontal-only changes must also trigger a coherent publication.
    bus.dut.input_horizontal_wheel=7;
    bus.until([&] { return bus.memory[address(TM_KEYBOARD_OFFSET)] == input_sequence+6; });
    CHECK(bus.memory[address(TM_HORIZONTAL_WHEEL_OFFSET)] == 7);
    bus.dut.vblank_start = 0;
    bus.memory[address(TM_VIDEO_PUBLISH_OFFSET)] = 0;
    bus.memory[address(TM_SESSION_REQUEST_OFFSET)] = 10;
    bus.until([&] { return bus.memory[address(TM_SESSION_ACK_OFFSET)] == 10; });
    tm_exchange exchange;
    tm_exchange_init(&exchange);
    for (unsigned frame = 0; frame < 12; ++frame) {
        unsigned buffer;
        uint32_t publication;
        CHECK(tm_exchange_acquire(&exchange, bus.memory[address(TM_VIDEO_PRESENTED_OFFSET)], &buffer) == 1);
        uint32_t source = address(buffer ? TM_BUFFER1_OFFSET : TM_BUFFER0_OFFSET);
        for (unsigned i = 0; i < TM_FRAME_BYTES / 8; ++i)
            bus.memory[source + i] = ((uint64_t)frame << 48) | ((uint64_t)i << 16) | 0xABCD;
        CHECK(tm_exchange_publish(&exchange, &publication) == 0);
        bus.memory[address(TM_VIDEO_PUBLISH_OFFSET)] = publication;
        // Let publication polling run, then issue a genuine vblank pulse.
        for (unsigned i = 0; i < 100; ++i) bus.step();
        bus.dut.vblank_start = 1; bus.step(); bus.dut.vblank_start = 0;
        bus.until([&] { return bus.memory[address(TM_VIDEO_PRESENTED_OFFSET)] == publication; });
        CHECK(bus.frame_writes == 0 && bus.dut.frame_bank == buffer);
        for (unsigned i = 0; i < TM_FRAME_BYTES / 8; ++i)
            CHECK(bus.frames[buffer][i] == bus.memory[source + i]);
    }
    // Runtime restart invalidates displayed state and requires a new sequence.
    bus.memory[address(TM_VIDEO_PUBLISH_OFFSET)] = 0;
    bus.memory[address(TM_SESSION_REQUEST_OFFSET)] = 11;
    bus.until([&] { return bus.memory[address(TM_SESSION_ACK_OFFSET)] == 11; });
    bus.until([&] { return bus.memory[address(TM_VIDEO_PRESENTED_OFFSET)] == 0; });
    CHECK(!bus.dut.source_valid);
    std::puts("DDR stalls, burst/frame data, stale boot/restart and coherent changed-only input snapshots verified");
}
