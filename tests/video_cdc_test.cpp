#include "Vvideo_cdc_fixture.h"
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
static uint32_t color(unsigned x, unsigned y, unsigned generation) {
    return ((x + generation * 17) & 255) | (((y * 43 + generation * 7) & 255) << 8) |
           (((x ^ y ^ (generation * 63)) & 255) << 16) | (generation << 24);
}
static uint64_t word(unsigned index, unsigned generation) {
    unsigned pixel = index * 2;
    return color(pixel % 256, pixel / 256, generation) |
           (uint64_t(color((pixel + 1) % 256, (pixel + 1) / 256, generation)) << 32);
}
struct Simulation {
    Vvideo_cdc_fixture dut;
    std::unordered_map<uint32_t, uint64_t> memory;
    std::deque<uint64_t> response;
    std::array<unsigned, 2> generation{}, words{};
    uint64_t now = 0, next_sys = 4096, next_vid;
    unsigned sys_cycles = 0, vid_cycles = 0, latency = 0;
    unsigned completed = 0, pixels = 0, bank_changes = 0, flushes = 0, active_completions = 0;
    bool held = false, held_write = false;
    uint32_t held_address = 0;
    uint64_t held_data = 0;
    unsigned held_burst = 0;
    explicit Simulation(unsigned phase) : next_vid(17500 + phase) {
        dut.clk_sys = dut.clk_vid = dut.ce_pix = 0;
        dut.reset = 1; dut.eval();
    }
    void step() {
        now = next_sys < next_vid ? next_sys : next_vid;
        bool sys_event = now == next_sys, vid_event = now == next_vid;
        bool sys_rise = sys_event && !dut.clk_sys;
        bool vid_rise = vid_event && !dut.clk_vid;
        if (sys_rise) {
            dut.ddr_busy = sys_cycles % 29 < 7 || sys_cycles % 1999 < 101;
            dut.ddr_dout_ready = !response.empty() && !latency && sys_cycles % 7 != 0;
            if (dut.ddr_dout_ready) dut.ddr_dout = response.front();
        }
        if (vid_rise) {
            dut.ce_pix = uint64_t(vid_cycles + 1) * 5360520 / 24576000 !=
                         uint64_t(vid_cycles) * 5360520 / 24576000;
        }
        dut.eval();
        bool video_reset = dut.reset_vid_active;
        bool visible = dut.visible;
        unsigned x = dut.x, y = dut.y, bank = dut.display_bank;
        bool pixel = dut.ce_pix;
        bool toggle = dut.frame_toggle;
        if (sys_rise) {
            if (held && !dut.reset_sys_active) {
                CHECK((dut.ddr_rd || dut.ddr_we) && dut.ddr_addr == held_address);
                CHECK(bool(dut.ddr_we) == held_write && dut.ddr_burstcnt == held_burst);
                if (held_write) CHECK(dut.ddr_din == held_data);
            }
            held = (dut.ddr_rd || dut.ddr_we) && dut.ddr_busy;
            if (held) {
                held_write = dut.ddr_we; held_address = dut.ddr_addr;
                held_data = dut.ddr_din; held_burst = dut.ddr_burstcnt;
            }
            CHECK(!(dut.ddr_rd && dut.ddr_we));
            if (dut.ddr_rd && !dut.ddr_busy) {
                CHECK(response.empty());
                for (unsigned i = 0; i < dut.ddr_burstcnt; ++i)
                    response.push_back(memory[dut.ddr_addr + i]);
                latency = 2 + sys_cycles % 17;
                // A slow first burst takes a copy across blanking and into
                // the visible raster. Exercise the pending-bank handshake
                // while the old complete frame must remain on screen.
                if (dut.ddr_burstcnt == 64 && (dut.ddr_addr == address(TM_BUFFER0_OFFSET) ||
                                               dut.ddr_addr == address(TM_BUFFER1_OFFSET)))
                    latency += 800000;
            }
            if (dut.ddr_we && !dut.ddr_busy) {
                CHECK(dut.ddr_be == 255 && dut.ddr_burstcnt == 1);
                memory[dut.ddr_addr] = dut.ddr_din;
            }
            if (dut.frame_write) {
                unsigned target = dut.write_bank, index = dut.write_address;
                CHECK(!dut.active || target != dut.display_bank);
                if (!index) { words[target] = 0; generation[target] = uint8_t(dut.write_data >> 24); }
                CHECK(index == words[target] && index < TM_FRAME_BYTES / 8);
                CHECK(dut.write_data == word(index, generation[target]));
                ++words[target];
            }
            if (dut.session_reset) ++flushes;
            if (dut.ddr_dout_ready) response.pop_front();
            if (latency) --latency;
            ++sys_cycles;
        }
        if (sys_event) { dut.clk_sys = !dut.clk_sys; next_sys += 4096; }
        if (vid_event) { dut.clk_vid = !dut.clk_vid; next_vid += 17500; }
        dut.eval();
        if (sys_rise && toggle != bool(dut.frame_toggle)) {
            CHECK(words[dut.write_bank] == TM_FRAME_BYTES / 8);
            ++completed;
            if (dut.y >= 40 && dut.y < 184) ++active_completions;
        }
        if (vid_rise) {
            if (video_reset) vid_cycles = 0;
            else {
                ++vid_cycles;
                if (bank != unsigned(dut.display_bank)) { CHECK(y >= 224); ++bank_changes; }
                if (pixel) {
                    uint32_t rgb = dut.r | (uint32_t(dut.g) << 8) | (uint32_t(dut.b) << 16);
                    if (visible && x < 256 && y >= 40 && y < 184) {
                        CHECK(rgb == (color(x, y - 40, generation[bank]) & 0xffffff));
                        ++pixels;
                    } else CHECK(!rgb);
                }
            }
        }
    }
    template<class Predicate> void until(Predicate predicate, unsigned budget = 5000000) {
        unsigned limit = sys_cycles + budget;
        while (!predicate() && sys_cycles < limit) step();
        CHECK(predicate());
    }
    void cycles(unsigned n) { unsigned end = sys_cycles + n; while (sys_cycles < end) step(); }
    void publish(tm_exchange &exchange, unsigned id) {
        unsigned buffer = 0;
        unsigned limit = sys_cycles + 5000000;
        int status;
        while (!(status = tm_exchange_acquire(&exchange, memory[address(TM_VIDEO_PRESENTED_OFFSET)], &buffer)) &&
               sys_cycles < limit) step();
        if (status != 1) std::fprintf(stderr, "acquire id=%u presented=%u expected=%u pending=%u cycles=%u\n",
            id, unsigned(memory[address(TM_VIDEO_PRESENTED_OFFSET)]), exchange.publication, exchange.pending, sys_cycles);
        CHECK(status == 1);
        uint32_t start = address(buffer ? TM_BUFFER1_OFFSET : TM_BUFFER0_OFFSET);
        for (unsigned i = 0; i < TM_FRAME_BYTES / 8; ++i) memory[start + i] = word(i, id);
        uint32_t publication;
        CHECK(tm_exchange_publish(&exchange, &publication) == 0);
        memory[address(TM_VIDEO_PUBLISH_OFFSET)] = publication;
    }
    void restart(unsigned nonce) {
        memory[address(TM_VIDEO_PUBLISH_OFFSET)] = 0;
        memory[address(TM_SESSION_REQUEST_OFFSET)] = nonce;
        until([&] { return memory[address(TM_SESSION_ACK_OFFSET)] == nonce; });
        // Match backend_connect: the ack write precedes cleared publication
        // and audio counters in the status burst; wait for all three.
        until([&] { return memory[address(TM_VIDEO_PRESENTED_OFFSET)] == 0 &&
                           memory[address(TM_AUDIO_READ_OFFSET)] == 0; });
        until([&] { return !dut.active && dut.frame_toggle == dut.consumed_toggle; });
    }
};
int main() {
    for (unsigned phase : {0u, 1u, 4095u, 17499u}) {
        Simulation s(phase);
        s.memory[address(TM_SESSION_REQUEST_OFFSET)] = 17;
        s.cycles(40); s.dut.reset = 0; s.dut.eval(); s.cycles(1000);
        CHECK(!s.dut.active && !s.completed);
        s.restart(18);
        tm_exchange exchange; tm_exchange_init(&exchange);
        for (unsigned id = 1; id <= 6; ++id) s.publish(exchange, id);
        s.until([&] { return s.memory[address(TM_VIDEO_PRESENTED_OFFSET)] == exchange.publication; });
        s.until([&] { return s.dut.frame_toggle == s.dut.consumed_toggle; });
        CHECK(s.completed == 6 && s.pixels >= 256 * 144 * 3);
        // Restart while a burst is copying a frame. The real controller must
        // finish its old read before acknowledging and clearing visibility.
        s.publish(exchange, 7);
        s.until([&] { return s.dut.frame_write && s.dut.write_address == 500; });
        s.restart(19);
        tm_exchange_init(&exchange);
        s.publish(exchange, 8);
        s.until([&] { return s.memory[address(TM_VIDEO_PRESENTED_OFFSET)] == exchange.publication; });
        // Restart again while completed frame ownership is awaiting scanout.
        s.publish(exchange, 9);
        s.until([&] { return s.dut.frame_toggle != s.dut.consumed_toggle; });
        s.restart(20);
        tm_exchange_init(&exchange);
        s.publish(exchange, 10);
        s.until([&] { return s.memory[address(TM_VIDEO_PRESENTED_OFFSET)] == exchange.publication; });
        s.until([&] { return s.dut.frame_toggle == s.dut.consumed_toggle; });
        unsigned before = s.pixels; s.cycles(4000000);
        CHECK(s.pixels >= before + 256 * 144);
        CHECK(s.flushes == 3 && s.completed >= 9 && s.bank_changes >= 4 && s.active_completions >= 6);
        std::printf("Independent clocks phase=%u: %u complete copies (%u during active raster), %u verified RGB pixels, %u vblank switches, %u one-cycle session flushes\n",
                    phase, s.completed, s.active_completions, s.pixels, s.bank_changes, s.flushes);
    }
}
