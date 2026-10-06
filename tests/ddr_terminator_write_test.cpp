#include "Vddr_terminator_write.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "terminator write failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
struct Bus {
    Vddr_terminator_write dut;
    unsigned accepted = 0, unmasked = 0;
    void edge(bool busy) {
        dut.clk = 0; dut.busy = busy; dut.eval();
        if (dut.write_request && !busy) {
            CHECK(dut.address == 0x100);
            ++accepted;
            if (dut.byteenable) ++unmasked;
        }
        dut.clk = 1; dut.eval();
    }
};
int main() {
    unsigned cases = 0;
    for (unsigned burst : {1u, 2u, 4u, 8u, 64u, 128u})
    for (unsigned before : std::set<unsigned>{0u, burst > 1 ? 1u : 0u, burst - 1})
    for (unsigned stall : {1u, 2u, 4u, 17u}) {
        Bus bus;
        bus.dut.reset = 1;
        for (unsigned i = 0; i < 4; ++i) bus.edge(false);
        bus.dut.reset = 0; bus.edge(false);
        bus.dut.core_burst = burst; bus.dut.core_write = 1;
        for (unsigned i = 0; i < before; ++i) bus.edge(false);
        // Observe the stalled beat before synchronous reset takes ownership.
        for (unsigned i = 0; i < stall; ++i) bus.edge(true);
        bus.dut.reset = 1; bus.edge(true);
        CHECK(bus.dut.owns_bus);
        bus.dut.core_write = 0;
        for (unsigned beat = before; beat < burst; ++beat) {
            for (unsigned i = 0; i < stall; ++i) {
                bus.dut.clk = 0; bus.dut.busy = 1; bus.dut.eval();
                CHECK(bus.dut.write_request && bus.dut.address == 0x100);
                CHECK(bus.dut.burst == burst && bus.dut.byteenable == 0);
                bus.edge(true);
            }
            bus.edge(false);
        }
        CHECK(bus.accepted == burst && bus.unmasked == before);
        for (unsigned i = 0; i < 4; ++i) bus.edge(false);
        CHECK(!bus.dut.write_request && bus.accepted == burst);
        bus.dut.reset = 0; bus.edge(false);
        bus.dut.core_burst = 1; bus.dut.core_write = 1;
        bus.edge(false); bus.dut.core_write = 0;
        CHECK(bus.accepted == burst + 1 && bus.unmasked == before + 1);
        ++cases;
    }
    std::printf("%u single/burst write reset cases: all stalled beats retained, cancelled data masked, one acceptance per beat\n", cases);
}
