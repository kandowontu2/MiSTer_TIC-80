#include "Vplatform_audio_reset_fixture.h"
#include "verilated.h"
#include <cstdlib>
#include <iostream>

static unsigned cases = 0, pulses = 0, stopped = 0;
static unsigned long long observations = 0;
static void require(bool good, const char *message) {
    if (!good) {
        std::cerr << "RESET_CONTRACT_FAIL case=" << cases << " " << message << '\n';
        std::exit(1);
    }
}

struct Contract {
    Vplatform_audio_reset_fixture dut;
    unsigned release_edges = 0;
    unsigned phase = 0;
    bool old_clock = false;
    void set(bool clock, bool reset) {
        dut.clk = clock; dut.reset_async = reset; dut.eval();
        if (reset) release_edges = 0;
        else if (!old_clock && clock && release_edges < 3) ++release_edges;
        require(bool(dut.reset_audio) == (reset || release_edges < 3),
                "reset must assert without a clock and release after exactly three rising edges");
        old_clock = clock; ++observations;
    }
    void step(bool reset = false) {
        set(phase >= 32, reset); phase = (phase + 1) & 63;
    }
    void warm() { set(false, false); for (unsigned n = 0; n < 8*64; ++n) step(); }
    void pulse(unsigned width) {
        ++pulses;
        for (unsigned n = 0; n < width; ++n) step(true);
    }
};

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    for (unsigned phase = 0; phase < 64; ++phase) {
        for (unsigned width : {1u, 2u, 7u, 33u, 65u}) {
            ++cases; Contract c; c.warm();
            for (unsigned n = 0; n < phase; ++n) c.step();
            c.pulse(width);
            for (unsigned n = 0; n < 4*64; ++n) c.step();
        }
    }
    for (unsigned edges = 1; edges <= 2; ++edges) {
        for (unsigned phase = 0; phase < 64; ++phase) {
            ++cases; Contract c; c.warm(); c.pulse(1);
            while (c.release_edges < edges) c.step();
            for (unsigned n = 0; n < phase; ++n) c.step();
            c.pulse(1);
            for (unsigned n = 0; n < 4*64; ++n) c.step();
        }
    }
    for (bool level : {false, true}) {
        ++cases; ++stopped; Contract c; c.warm(); c.set(level, false);
        c.set(level, true); ++pulses;
        c.set(level, false);
        for (unsigned n = 0; n < 500; ++n) c.set(level, false);
        require(c.dut.reset_audio, "reset disappeared while the clock was stopped");
        c.phase = level ? 33 : 0;
        for (unsigned n = 0; n < 4*64; ++n) c.step();
    }
    require(cases == 450 && stopped == 2 && observations > 300000, "insufficient coverage");
    std::cout << "{\"passed\":true,\"cases\":" << cases << ",\"pulses\":" << pulses
              << ",\"observations\":" << observations << ",\"stopped_clock_cases\":" << stopped
              << ",\"subcycle_assertion_checked\":true,\"physical_qualified\":false}\n";
}
