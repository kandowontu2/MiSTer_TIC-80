#include "Vtic80_pixel_enable.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
int main() {
    Vtic80_pixel_enable dut;
    dut.clk = 0; dut.reset = 1; dut.eval();
    dut.clk = 1; dut.eval(); dut.reset = 0;
    uint64_t pixels = 0, previous = 0, rasters = 0;
    unsigned shortest = 9, longest = 0;
    // Ten seconds: compare cumulative pulses with an independent rational
    // frequency calculation and check every interval and frame boundary.
    for (uint64_t clock = 1; clock <= 245760000; ++clock) {
        dut.clk = 0; dut.eval();
        bool pixel = dut.ce_pix;
        dut.clk = 1; dut.eval();
        if (pixel) {
            ++pixels;
            if (previous) {
                unsigned interval = clock - previous;
                CHECK(interval == 4 || interval == 5);
                if (interval < shortest) shortest = interval;
                if (interval > longest) longest = interval;
            }
            previous = clock;
            if (pixels % (341 * 262) == 0) {
                ++rasters;
                CHECK(clock == rasters * 409600);
            }
        }
        CHECK(pixels == clock * 5360520ULL / 24576000ULL);
    }
    CHECK(pixels == 53605200 && rasters == 600 && shortest == 4 && longest == 5);
    dut.clk = 0; dut.reset = 1; dut.eval(); CHECK(!dut.ce_pix);
    dut.clk = 1; dut.eval(); dut.reset = 0;
    for (unsigned clock = 1; clock <= 5; ++clock) {
        dut.clk = 0; dut.eval(); CHECK(!!dut.ce_pix == (clock == 5));
        dut.clk = 1; dut.eval();
    }
    std::puts("Exact 600 raster periods, 800 playback samples per raster, pixel gaps 4/5 clocks and reset verified");
}
