#include "Vtic80_scanout.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static uint32_t color(unsigned x, unsigned y, unsigned bank) {
    return ((x + bank*17)&255) | (((y*43+bank*7)&255)<<8) |
           (((x ^ y ^ (bank*63))&255)<<16) | (((x*11+y*3)&255)<<24);
}
struct Simulation {
    Vtic80_scanout dut;
    unsigned cycles = 0, x = 0, y = 0, visible_pixels = 0;
    bool previous_bank = 0;
    void step() {
        dut.clk_sys = 0; dut.clk_vid = 0;
        dut.ce_pix = ((uint64_t)(cycles + 1) * 5360520 / 24576000) !=
                     ((uint64_t)cycles * 5360520 / 24576000);
        dut.eval();
        bool ce = dut.ce_pix;
        bool valid = dut.display_valid;
        unsigned bank = dut.display_bank;
        dut.clk_sys = 1; dut.clk_vid = 1; dut.eval();
        if (dut.reset) { x = y = 0; cycles = 0; }
        else if (ce) {
            CHECK(dut.de == (x < 256 && y >= 40 && y < 184));
            CHECK(dut.hs == !(x >= 277 && x < 302));
            CHECK(dut.vs == !(y >= 237 && y < 240));
            if (x < 256 && y >= 40 && y < 184 && valid) {
                uint32_t p = color(x, y - 40, bank);
                CHECK(dut.r == (p & 255));
                CHECK(dut.g == ((p >> 8) & 255));
                CHECK(dut.b == ((p >> 16) & 255));
                ++visible_pixels;
            } else CHECK(!dut.r && !dut.g && !dut.b);
            if (++x == 341) { x = 0; if (++y == 262) y = 0; }
        }
        if (dut.display_bank != previous_bank) CHECK(y >= 224);
        previous_bank = dut.display_bank;
        if (!dut.reset) ++cycles;
    }
    void fill(unsigned bank) {
        dut.frame_write = 1; dut.frame_write_bank = bank;
        for (unsigned index = 0; index < 256 * 144 / 2; ++index) {
            uint64_t word = 0;
            for (unsigned sub = 0; sub < 2; ++sub) {
                unsigned pixel = index * 2 + sub;
                word |= (uint64_t)color(pixel % 256, pixel / 256, bank) << (sub * 32);
            }
            dut.frame_write_address = index; dut.frame_write_data = word; step();
        }
        dut.frame_write = 0;
    }
};
int main() {
    Simulation sim;
    sim.dut.reset = 1; sim.dut.frame_write = 0; sim.dut.source_valid = 0;
    sim.dut.frame_bank = 0; sim.dut.frame_toggle = 0;
    sim.step(); sim.dut.reset = 0;
    sim.fill(0);
    sim.dut.source_valid = 1; sim.dut.frame_toggle = 1;
    while (!(sim.y == 80 && sim.x == 50 && sim.dut.display_valid)) sim.step();
    sim.fill(1); sim.dut.frame_bank = 1; sim.dut.frame_toggle = 0;
    while (!sim.dut.display_bank) sim.step();
    unsigned target = sim.visible_pixels + 256 * 144 * 2;
    while (sim.visible_pixels < target) sim.step();
    CHECK(sim.visible_pixels > 256 * 144 * 2);
    sim.dut.source_valid = 0;
    for (unsigned i = 0; i < 20; ++i) sim.step();
    CHECK(!sim.dut.display_valid);
    std::puts("RGB pixels, letterbox, sync timing, vblank bank switching verified");
}
