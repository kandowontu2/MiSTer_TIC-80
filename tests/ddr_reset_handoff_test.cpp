#include "Vddr_reset_handoff.h"
#include "tic80_mister/memory_map.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <unordered_map>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "reset handoff failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static uint32_t addr(uint32_t offset) { return (TM_PHYSICAL_BASE + offset) >> 3; }
struct Bus {
    Vddr_reset_handoff dut;
    std::unordered_map<uint32_t, uint64_t> memory;
    std::deque<uint64_t> response;
    unsigned cycles = 0, accepted_reads = 0, accepted_writes = 0;
    bool held = false, held_write = false;
    uint32_t held_address = 0;
    unsigned held_burst = 0, held_be = 0;
    uint64_t held_data = 0;
    void check_hold() {
        if (!held) return;
        CHECK(dut.read_request || dut.write_request);
        CHECK(bool(dut.write_request) == held_write);
        CHECK(dut.address == held_address && dut.burstcount == held_burst);
        // The platform intentionally masks a cancelled write after takeover.
        if (held_write && !dut.platform_owns_bus) {
            CHECK(dut.byteenable == held_be && dut.writedata == held_data);
        }
    }
    void low(bool busy) {
        dut.clk = 0;
        dut.waitrequest = busy;
        dut.readdatavalid = !response.empty();
        dut.readdata = response.empty() ? 0 : response.front();
        dut.eval();
    }
    void edge(bool busy) {
        low(busy);
        check_hold();
        bool delivering = dut.readdatavalid;
        held = (dut.read_request || dut.write_request) && busy;
        if (held) {
            held_write = dut.write_request;
            held_address = dut.address; held_burst = dut.burstcount;
            held_be = dut.byteenable; held_data = dut.writedata;
        }
        if (dut.read_request && !busy) {
            CHECK(response.empty());
            for (unsigned i = 0; i < dut.burstcount; ++i) response.push_back(memory[dut.address + i]);
            ++accepted_reads;
        }
        if (dut.write_request && !busy) {
            CHECK(dut.burstcount == 1);
            uint64_t &word = memory[dut.address];
            for (unsigned i = 0; i < 8; ++i) if (dut.byteenable & (1u << i)) {
                uint64_t mask = 0xffULL << (i * 8);
                word = (word & ~mask) | (dut.writedata & mask);
            }
            ++accepted_writes;
        }
        // Consume the old response, not a newly accepted read's first beat.
        if (delivering) response.pop_front();
        dut.clk = 1; dut.eval(); ++cycles;
    }
    bool target(unsigned kind) {
        if (kind == 0) return dut.read_request && dut.address == addr(TM_SESSION_REQUEST_OFFSET);
        if (kind == 1) return dut.write_request && dut.address == addr(TM_IDENTITY_OFFSET);
        if (kind == 2) return dut.write_request && dut.address == addr(TM_CART_DATA_OFFSET);
        if (kind == 3) return dut.read_request && dut.burstcount == 64;
        return dut.read_request && dut.address >= addr(TM_AUDIO_RING_OFFSET) && dut.address < addr(TM_CART_SOURCE_OFFSET);
    }
    void initialize() {
        dut.reset = 1;
        for (unsigned i = 0; i < 8; ++i) edge(false);
        dut.reset = 0;
    }
};
int main() {
    for (unsigned kind = 0; kind < 5; ++kind)
    for (unsigned stall : {1u, 2u, 4u, 11u, 37u})
    for (bool high_phase : {false, true})
    for (bool sample_delay : {false, true}) {
        std::printf("testing reset kind=%u stall=%u phase=%s sample_delay=%u\n", kind, stall, high_phase ? "high" : "low", sample_delay);
        std::fflush(stdout);
        Bus bus;
        bus.dut.reset_sample_delay = sample_delay;
        bus.initialize();
        unsigned sent = 0;
        bool begun = false, nonce = false;
        for (unsigned i = 0; i < 300000; ++i) {
            bus.dut.ioctl_wr = 0;
            if (kind == 2 && i > 200) {
                if (!begun) { bus.dut.ioctl_download = 1; begun = true; }
                else if (!bus.dut.ioctl_wait && sent < 8) {
                    bus.dut.ioctl_wr = 1; bus.dut.ioctl_addr = sent;
                    bus.dut.ioctl_dout = 0xa0 + sent++;
                }
            }
            if ((kind == 3 || kind == 4) && i > 200 && !nonce) {
                bus.memory[addr(TM_SESSION_REQUEST_OFFSET)] = 1;
                bus.memory[addr(TM_VIDEO_PUBLISH_OFFSET)] = 2;
                bus.memory[addr(TM_AUDIO_WRITE_OFFSET)] = kind == 4 ? 1600 : 0;
                nonce = true;
            }
            bus.low(false);
            if (bus.target(kind)) break;
            bus.edge(false);
        }
        CHECK(bus.target(kind));
        bus.edge(true); // Latch a real, unaccepted command under backpressure.
        if (!high_phase) bus.low(true);
        unsigned reads = bus.accepted_reads, writes = bus.accepted_writes;
        bus.dut.reset = 1; bus.dut.ioctl_download = 0; bus.dut.ioctl_wr = 0;
        bus.dut.eval();
        bus.check_hold(); // Also check between clocks, when reset arrives.
        for (unsigned i = 0; i < stall; ++i) bus.edge(true);
        bus.edge(false);
        CHECK(bus.accepted_reads - reads == (kind == 0 || kind == 3 || kind == 4 ? 1u : 0u));
        CHECK(bus.accepted_writes - writes == (kind == 1 || kind == 2 ? 1u : 0u));
        for (unsigned i = 0; i < 90; ++i) bus.edge(false);
        CHECK(bus.response.empty());
        bus.memory[addr(TM_IDENTITY_OFFSET)] = 0;
        bus.dut.reset = 0;
        for (unsigned i = 0; i < 1000; ++i) bus.edge(false);
        CHECK(uint32_t(bus.memory[addr(TM_IDENTITY_OFFSET)]) == TM_MAGIC);
        CHECK(uint32_t(bus.memory[addr(TM_SESSION_ACK_OFFSET)]) == 0);
        std::printf("reset kind=%u stall=%u phase=%s held, accepted once, drained and restarted\n", kind, stall, high_phase ? "high" : "low");
    }
    std::puts("100 platform/core reset handoffs passed");
}
