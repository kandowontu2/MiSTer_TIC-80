#include "Vtic80_video_top.h"
#include "tic80_mister/exchange.h"
#include "tic80_mister/memory_map.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <unordered_map>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static uint32_t address(uint32_t offset) { return (TM_PHYSICAL_BASE + offset) >> 3; }
static uint32_t sample(unsigned i) { return (uint16_t)(i+1) | ((uint32_t)(uint16_t)~(i+1) << 16); }
struct Simulation {
    Vtic80_video_top dut;
    std::unordered_map<uint32_t, uint64_t> memory;
    std::deque<uint64_t> response;
    unsigned cycle = 0, latency = 0, vid_phase = 0, audio_phase = 0;
    unsigned vid_div = 0, x = 0, y = 0, pixels = 0, played = 0;
    uint32_t previous_audio = 0, frame_color = 0;
    bool stalled = false, write = false;
    bool previous_hs = true, previous_vs = true, aligned = false;
    uint32_t held_address = 0;
    uint64_t held_data = 0;
    unsigned held_burst = 0, held_be = 0;
    void step() {
        dut.clk_sys = dut.clk_vid = dut.clk_audio = 0;
        dut.ddr_busy = cycle % 11 < 3;
        dut.ddr_dout_ready = !response.empty() && !latency && cycle % 5 != 0;
        if (dut.ddr_dout_ready) dut.ddr_dout = response.front();
        vid_phase += 24576;
        audio_phase += 24576;
        bool video = vid_phase >= 105000, audio = audio_phase >= 105000;
        if (video) vid_phase -= 105000;
        if (audio) audio_phase -= 105000;
        dut.ce_pix = video && (((uint64_t)(vid_div + 1) * 5360520 / 24576000) !=
                              ((uint64_t)vid_div * 5360520 / 24576000));
        dut.eval();
        if (stalled && !dut.reset) {
            CHECK((dut.ddr_rd || dut.ddr_we) && dut.ddr_addr == held_address);
            CHECK(!!dut.ddr_we == write && dut.ddr_burstcnt == held_burst);
            CHECK(dut.ddr_be == held_be);
            if (write) CHECK(dut.ddr_din == held_data);
        }
        stalled = (dut.ddr_rd || dut.ddr_we) && dut.ddr_busy && !dut.reset;
        if (stalled) {
            write = dut.ddr_we; held_address = dut.ddr_addr;
            held_data = dut.ddr_din; held_burst = dut.ddr_burstcnt; held_be = dut.ddr_be;
        }
        if (dut.ddr_rd && !dut.ddr_busy && !dut.reset) {
            CHECK(response.empty());
            for (unsigned i = 0; i < dut.ddr_burstcnt; ++i) response.push_back(memory[dut.ddr_addr+i]);
            latency = 3 + cycle % 7;
        }
        if (dut.ddr_we && !dut.ddr_busy && !dut.reset) {
            uint64_t &word = memory[dut.ddr_addr];
            for (unsigned i = 0; i < 8; ++i) if (dut.ddr_be & (1 << i)) {
                uint64_t mask = 0xFFULL << (i*8);
                word = (word & ~mask) | (dut.ddr_din & mask);
            }
        }
        if (dut.ddr_dout_ready) response.pop_front();
        if (latency) --latency;
        bool pixel = dut.ce_pix, visible = dut.active;
        dut.clk_sys = 1; dut.clk_vid = video; dut.clk_audio = audio;
        dut.eval();
        if (video) ++vid_div;
        uint32_t pcm = dut.audio_l | ((uint32_t)dut.audio_r << 16);
        if (pcm != previous_audio) {
            if (pcm) { CHECK(pcm == sample(played)); ++played; }
            else CHECK(played == TM_AUDIO_CAPACITY);
            previous_audio = pcm;
        }
        if (!dut.reset && pixel) {
            // Synchronize the independent monitor to output sync edges.
            if (previous_hs && !dut.hs) x = 277;
            if (!previous_hs && dut.hs) x = 302;
            if (previous_vs && !dut.vs) { y = 237; aligned = true; }
            previous_hs = dut.hs; previous_vs = dut.vs;
            uint32_t rgb = dut.r | ((uint32_t)dut.g << 8) | ((uint32_t)dut.b << 16);
            if (aligned && dut.de && x < 256 && y >= 40 && y < 184 && visible) {
                if (x == 0 && y == 40) frame_color = rgb;
                CHECK(rgb == frame_color); // no partial old/new frame
                ++pixels;
            }
            if (++x == 341) { x = 0; if (++y == 262) y = 0; }
        }
        ++cycle;
    }
};
static void synchronized_resets() {
    Vtic80_video_top d;
    d.clk_sys=d.clk_vid=d.clk_audio=0; d.reset=0; d.eval();
    d.reset=1; d.eval();
    CHECK(d.reset_sys_active && d.reset_vid_active);
    d.reset=0; d.eval();
    for(unsigned i=0;i<4;++i) {
        d.clk_sys=0; d.eval(); d.clk_sys=1; d.eval();
        CHECK(!!d.reset_sys_active==(i<3));
        // System clocks alone cannot release the video-domain reset.
        CHECK(d.reset_vid_active);
    }
    for(unsigned i=0;i<2;++i) {
        d.clk_vid=0; d.eval(); d.clk_vid=1; d.eval();
        CHECK(!!d.reset_vid_active==(i<1));
    }
    d.clk_sys=d.clk_vid=0; d.eval(); d.reset=1; d.eval();
    CHECK(d.reset_vid_active && !d.reset_sys_active);
    for(unsigned i=0;i<4;++i) {
        d.clk_sys=0; d.eval(); d.clk_sys=1; d.eval();
        CHECK(!!d.reset_sys_active==(i==3));
    }
    puts("Shared resets: independent clock-domain releases and four-cycle DDR assertion handoff preserved");
}
int main() {
    synchronized_resets();
    Simulation s;
    s.dut.reset = 1;
    for (unsigned i = 0; i < 100; ++i) s.step();
    s.dut.reset = 0;
    // Track the counter phase after the two-edge synchronized reset release.
    // Start the raster audit only after a complete carrier cycle.
    s.memory[address(TM_SESSION_REQUEST_OFFSET)] = 9;
    for (unsigned i = 0; i < 1000; ++i) s.step();
    s.memory[address(TM_SESSION_REQUEST_OFFSET)] = 10;
    while (s.memory[address(TM_SESSION_ACK_OFFSET)] != 10) s.step();
    for (unsigned i = 0; i < TM_AUDIO_CAPACITY/2; ++i)
        s.memory[address(TM_AUDIO_RING_OFFSET)+i] = (uint64_t)sample(i*2) | ((uint64_t)sample(i*2+1) << 32);
    s.memory[address(TM_AUDIO_WRITE_OFFSET)] = TM_AUDIO_CAPACITY;
    // Exercise the second F0 extension (PNG=0x40) on the same stalled bus
    // as audio/video. Native TIC=0 is covered by the cart-loader fixture.
    s.memory[address(TM_CART_DATA_OFFSET)] = 0xCCCCCCCCCCCCCCCCULL;
    s.memory[address(TM_CART_DATA_OFFSET)+1] = 0xCCCCCCCCCCCCCCCCULL;
    const char path[]="/media/fat/games/TIC-80/with spaces/cart.png";
    std::vector<uint8_t> source{(uint8_t)TM_CART_SOURCE_MAGIC,(uint8_t)(TM_CART_SOURCE_MAGIC>>8),
        (uint8_t)(TM_CART_SOURCE_MAGIC>>16),(uint8_t)(TM_CART_SOURCE_MAGIC>>24),0x40,0,sizeof(path),0};
    source.insert(source.end(),path,path+sizeof path);
    s.dut.ioctl_index=0xfe; s.dut.ioctl_download=1; s.step();
    for(unsigned i=0;i<source.size();++i) {
        while(s.dut.ioctl_wait) s.step();
        s.dut.ioctl_addr=i; s.dut.ioctl_dout=source[i]; s.dut.ioctl_wr=1; s.step();
        s.dut.ioctl_wr=0; s.step();
    }
    s.dut.ioctl_download=0; for(unsigned i=0;i<1000;++i) s.step();
    s.dut.ioctl_index = 0x40; s.dut.ioctl_download = 1;
    s.step(); // FIO_FILE_TX start is a distinct command before file bytes.
    for (unsigned i = 0; i < 13; ++i) {
        while (s.dut.ioctl_wait) s.step();
        s.dut.ioctl_addr = i; s.dut.ioctl_dout = i + 1;
        s.dut.ioctl_wr = 1; s.step(); s.dut.ioctl_wr = 0; s.step();
    }
    s.dut.ioctl_download = 0;
    unsigned cart_limit = s.cycle + 10000;
    while ((s.memory[address(TM_CART_META_OFFSET)] & 3) != 2 && s.cycle < cart_limit) s.step();
    CHECK(s.memory[address(TM_CART_META_OFFSET)] == ((uint64_t)13 << 32 | 6));
    CHECK(s.memory[address(TM_CART_SOURCE_OFFSET)] == ((uint64_t)sizeof(path)<<32 | 6));
    for(unsigned i=0;i<sizeof path;++i)
        CHECK((uint8_t)(s.memory[address(TM_CART_SOURCE_OFFSET+8+(i&~7))]>>(8*(i&7)))==(uint8_t)path[i]);
    CHECK(s.memory[address(TM_CART_DATA_OFFSET)] == 0x0807060504030201ULL);
    CHECK(s.memory[address(TM_CART_DATA_OFFSET)+1] == 0xCCCCCC0D0C0B0A09ULL);
    s.memory[address(TM_CART_ACK_OFFSET)] = 6;
    s.dut.ps2_key=0x61C; s.step(); s.step(); // A pressed
    s.dut.ps2_key=0x283; s.step(); s.step(); // F7 pressed, toggle flips again
    s.dut.ps2_mouse=(1<<24)|(3<<16)|(5<<8)|8;
    s.dut.ps2_mouse_ext=0xFE;
    for(unsigned i=0;i<1000;++i) s.step();
    CHECK(!(s.memory[address(TM_KEYBOARD_OFFSET)]&1));
    CHECK(s.memory[address(TM_KEYBOARD_BITS_OFFSET)] == (1ULL<<28));
    CHECK(s.memory[address(TM_KEYBOARD_BITS_OFFSET)+2] == (1ULL<<3));
    CHECK(s.memory[address(TM_MOUSE_OFFSET)] == 0xFFFFFFFE0000417DULL);
    tm_exchange exchange;
    tm_exchange_init(&exchange);
    unsigned sent = 0;
    while (s.cycle < 14000000 && (sent < 6 || s.played < TM_AUDIO_CAPACITY || s.previous_audio ||
           s.memory[address(TM_VIDEO_PRESENTED_OFFSET)] != exchange.publication)) {
        if (sent < 6) {
            unsigned buffer;
            int acquired = tm_exchange_acquire(&exchange, s.memory[address(TM_VIDEO_PRESENTED_OFFSET)], &buffer);
            CHECK(acquired >= 0);
            if (acquired) {
                ++sent;
                uint32_t color = (sent*7) | ((sent*29+17)<<8) | ((sent*11+31)<<16) | (sent<<24);
                uint64_t word = color * 0x0000000100000001ULL;
                for (unsigned i = 0; i < TM_FRAME_BYTES/8; ++i)
                    s.memory[address(buffer ? TM_BUFFER1_OFFSET : TM_BUFFER0_OFFSET)+i] = word;
                uint32_t publication;
                CHECK(tm_exchange_publish(&exchange, &publication) == 0);
                s.memory[address(TM_VIDEO_PUBLISH_OFFSET)] = publication;
            }
        }
        s.step();
    }
    CHECK(sent == 6 && s.played == TM_AUDIO_CAPACITY && !s.previous_audio);
    CHECK(s.memory[address(TM_VIDEO_PRESENTED_OFFSET)] == exchange.publication);
    CHECK(s.pixels > TM_WIDTH * TM_HEIGHT * 2);
    s.dut.osd_open=1;
    for(unsigned i=0;i<1000;++i) s.step();
    CHECK(!(s.memory[address(TM_KEYBOARD_OFFSET)]&1));
    for(unsigned i=0;i<8;++i) CHECK(s.memory[address(TM_KEYBOARD_BITS_OFFSET)+i]==0);
    std::puts("Combined DDR/shared-video-audio/cart/input transport survives stalled DDR without mixed snapshots/frames or PCM loss");
}
