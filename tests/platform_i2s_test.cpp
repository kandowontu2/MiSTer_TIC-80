#include "Vplatform_i2s_fixture.h"
#include "verilated.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

static uint64_t cycle = 0, words = 0, changes = 0, reset_coincidences = 0;
static uint64_t mid_reset_coincidences = 0, reset_states[4] = {};
static unsigned scenario = 0;
static bool safe_reset = false;
static uint64_t min_setup[2] = {999,999}, max_setup[2] = {}, min_hold[2] = {999,999}, max_hold[2] = {};
static uint64_t setup_observations[2] = {}, hold_observations[2] = {}, rises[2] = {};
static uint64_t enable_latency[2] = {};
static void require(bool good, const char *message) {
    if (!good) {
        std::cerr << "Scenario " << scenario << ", cycle " << cycle << ": " << message << '\n';
        std::exit(1);
    }
}

struct Observer {
    bool locked = false, previous_lr = true, pending_pair = false;
    uint16_t expected[2] = {}, pending[2] = {}, word = 0;
    unsigned bits = 0;
    bool valid_rise = false, await_fall = false, await_rise = false;
    uint64_t last_rise = 0, changed = 0, mode_changed = 0, reset_at = 0;
    int changed_mode = -1;
    uint64_t last_sample = 0;
    uint64_t last_clock_edge = 0;
    bool valid_clock_edge = false;
    bool valid_sample = false;
    bool in_reset = false, has_run = false;

    void step(Vplatform_i2s_fixture &dut, bool request_reset, unsigned rate) {
        dut.clk = 0; dut.reset = request_reset; dut.sample_rate = rate; dut.eval();
        const bool reset = dut.active_reset;
        const bool old_sclk = dut.sclk, old_lr = dut.lrclk, old_data = dut.sdata, old_ce = dut.ce;
        const uint16_t left = dut.left_sample, right = dut.right_sample;
        dut.clk = 1; dut.eval(); ++cycle;
        const bool rise = !old_sclk && dut.sclk, fall = old_sclk && !dut.sclk;
        if (reset) {
            if (!in_reset) ++reset_states[(unsigned(old_lr) << 1) | unsigned(old_sclk)];
            if (rise && old_lr != bool(dut.lrclk)) {
                ++reset_coincidences;
                if (has_run) ++mid_reset_coincidences;
            }
            in_reset = true;
            locked = false; previous_lr = true; pending_pair = false; word = 0; bits = 0;
            reset_at = cycle;
            valid_sample = false;
            if (!safe_reset || !has_run) {
                valid_rise = await_fall = await_rise = false;
                valid_clock_edge = false;
                if (!safe_reset) require(dut.sclk && dut.lrclk, "Reset did not establish high BCLK/LRCLK");
                return;
            }
        }
        else { in_reset = false; has_run = true; }
        const bool steady = cycle > mode_changed + 40 && cycle > reset_at + 40;
        if (safe_reset && has_run && cycle > reset_at + 16 && cycle <= reset_at + 32)
            require(dut.lrclk && !dut.sdata, "Short reset was lost or failed to establish silent right-channel data");
        if (rise || fall) {
            // Reset must not shorten or stretch BCLK high/low pulses. Rate
            // switches have their own transient interval, independent of reset.
            const bool clock_steady = cycle > mode_changed + 40 && (safe_reset || steady);
            if (valid_clock_edge && clock_steady && last_clock_edge > mode_changed + 40)
                require(cycle - last_clock_edge == (rate ? 4u : 8u), "BCLK pulse width changed during steady rate or reset");
            last_clock_edge = cycle; valid_clock_edge = true;
        }
        if (dut.sample_ce) {
            if (valid_sample && last_sample > mode_changed + 1024 && last_sample > reset_at + 1024)
                require(cycle - last_sample == (rate ? 256u : 512u), "Actual mixer sample-enable period is wrong");
            last_sample = cycle; valid_sample = true;
        }
        if (old_data != bool(dut.sdata) || old_lr != bool(dut.lrclk)) {
            ++changes;
            require(old_ce, "Data/LRCLK changed without the actual clock enable");
            require(old_sclk && dut.sclk, "Data/LRCLK changed at a BCLK transition");
            changed = cycle; await_fall = await_rise = true; changed_mode = steady ? int(rate) : -1;
            if (valid_rise) {
                const uint64_t gap = cycle - last_rise;
                require(gap >= 3 && gap <= 7, "Hold edge spacing outside 3..7 base cycles");
                if (steady) {
                    require(gap == (rate ? 3u : 7u), "Wrong steady-state hold edge spacing");
                    min_hold[rate] = std::min(min_hold[rate], gap); max_hold[rate] = std::max(max_hold[rate], gap);
                    ++hold_observations[rate];
                }
            }
        }
        // Reset abandons a partial word. Resume decoding at the first complete
        // left-frame boundary after the bounded reset-service interval.
        const bool decode = !reset && cycle > reset_at + (safe_reset ? 32u : 0u);
        if (decode && old_lr && !dut.lrclk) {
            pending_pair = true; pending[0] = left; pending[1] = right;
        }
        if (fall && await_fall) {
            require(cycle == changed + 1, "BCLK did not fall one base cycle after data update");
            await_fall = false;
        }
        if (rise) {
            ++rises[rate];
            if (valid_rise && steady && last_rise > mode_changed + 40 && last_rise > reset_at + 40)
                require(cycle - last_rise == (rate ? 8u : 16u), "Wrong steady-state BCLK period");
            valid_rise = true; last_rise = cycle;
            if (await_rise) {
                const uint64_t gap = cycle - changed;
                require(gap >= 5 && gap <= 9, "Setup edge spacing outside 5..9 base cycles");
                if (steady && changed_mode == int(rate)) {
                    require(gap == (rate ? 5u : 9u), "Wrong steady-state setup edge spacing");
                    min_setup[rate] = std::min(min_setup[rate], gap); max_setup[rate] = std::max(max_setup[rate], gap);
                    ++setup_observations[rate];
                }
                await_rise = false;
            }
            // I2S delays the channel selection by one BCLK: the first bit
            // captured after LRCLK changes is the previous channel's LSB.
            if (decode && locked) {
                word = uint16_t((word << 1) | dut.sdata); ++bits;
                if (bool(dut.lrclk) != previous_lr) {
                    require(bits == 16, "I2S channel word is not 16 bits");
                    require(word == expected[previous_lr ? 1 : 0], "Serialized sample/channel differs from the latched mixer sample");
                    ++words;
                } else require(bits < 16, "LRCLK did not change after the channel word");
            }
            if (decode && bool(dut.lrclk) != previous_lr) {
                if (!dut.lrclk) {
                    require(pending_pair, "Left-channel boundary lacks an atomic sample pair");
                    expected[0] = pending[0]; expected[1] = pending[1]; pending_pair = false; locked = true;
                }
                previous_lr = dut.lrclk; word = 0; bits = 0;
            }
            if (!decode) { locked = false; previous_lr = dut.lrclk; pending_pair = false; word = 0; bits = 0; }
        }
    }
};

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    require(argc == 1 || (argc == 2 && !std::strcmp(argv[1], "--require-safe-reset")), "Unknown test option");
    safe_reset = argc == 2;
    // Verify static initializer behavior after the simulator-only declaration
    // conversion. Clocked counter expressions and assignments are unchanged.
    for (unsigned rate = 0; rate < 2; ++rate) {
        Vplatform_i2s_fixture dut; Observer observe;
        dut.left_in = 0x8001; dut.right_in = 0x7fff;
        for (unsigned n = 0; n < 8; ++n) observe.step(dut, true, rate);
        const uint64_t start = cycle;
        while (!dut.audio_enabled && cycle - start < 4200000) observe.step(dut, false, rate);
        require(dut.audio_enabled && cycle - start > 4190000, "Audio mute counter was reinitialized or failed to progress");
        enable_latency[rate] = cycle - start; dut.final();
    }
    for (unsigned initial_rate = 0; initial_rate < 2; ++initial_rate) {
        for (unsigned phase = 0; phase < 512; ++phase) {
            for (unsigned reset_width : {1u, 2u, 7u, 33u}) {
                if (!safe_reset && reset_width == 33) continue;
                ++scenario;
                Vplatform_i2s_fixture dut; Observer observe;
                dut.left_in = 0x8001; dut.right_in = 0x7fff;
                for (unsigned n = 0; n < 8; ++n) observe.step(dut, true, initial_rate);
                for (unsigned n = 0; n < 8192 + phase; ++n) observe.step(dut, false, initial_rate);
                require(dut.left_sample == dut.left_in && dut.right_sample == dut.right_in, "ALSA mixer samples failed to settle");
                dut.left_in = 0x5aa5; dut.right_in = 0xa55a;
                observe.mode_changed = cycle;
                for (unsigned n = 0; n < 8192 + phase; ++n) observe.step(dut, false, !initial_rate);
                require(dut.left_sample == dut.left_in && dut.right_sample == dut.right_in, "Post-rate-change mixer samples failed to settle");
                for (unsigned n = 0; n < reset_width; ++n) observe.step(dut, true, !initial_rate);
                for (unsigned n = 0; n < 8192; ++n) observe.step(dut, false, !initial_rate);
                dut.final();
            }
        }
    }
    require(scenario == (safe_reset ? 4096u : 3072u) && words > 5000 && changes > 10000, "Insufficient serializer/reset coverage");
    if (safe_reset) require(mid_reset_coincidences == 0, "Active reset changed LRCLK at a rising BCLK edge");
    for (unsigned state = 0; state < 4; ++state) require(reset_states[state] > 0, "Reset entry state was not exercised");
    for (unsigned rate = 0; rate < 2; ++rate)
        require(setup_observations[rate] > 1000 && hold_observations[rate] > 1000, "Insufficient steady-state edge coverage");
    std::cout << "{\"scenarios\":" << scenario << ",\"base_cycles\":" << cycle
              << ",\"checked_channel_words\":" << words << ",\"data_or_lrclk_changes\":" << changes
              << ",\"reset_lrclk_bclk_rise_coincidences\":" << reset_coincidences
              << ",\"mid_session_reset_coincidences\":" << mid_reset_coincidences
              << ",\"reset_entry_lrclk_sclk_states\":[" << reset_states[0] << ',' << reset_states[1] << ',' << reset_states[2] << ',' << reset_states[3]
              << "],\"rates\":[";
    for (unsigned rate = 0; rate < 2; ++rate) {
        if (rate) std::cout << ',';
        std::cout << "{\"sample_rate_hz\":" << (rate ? 96000 : 48000)
                  << ",\"minimum_setup_base_cycles\":" << min_setup[rate] << ",\"maximum_setup_base_cycles\":" << max_setup[rate]
                  << ",\"minimum_hold_base_cycles\":" << min_hold[rate] << ",\"maximum_hold_base_cycles\":" << max_hold[rate]
                  << ",\"setup_observations\":" << setup_observations[rate] << ",\"hold_observations\":" << hold_observations[rate]
                  << ",\"bclk_rises\":" << rises[rate] << ",\"audio_enable_latency_base_cycles\":" << enable_latency[rate] << '}';
    }
    std::cout << "],\"safe_reset_edge_checks\":" << (safe_reset ? "true" : "false")
              << ",\"passed\":true,\"reset_transients_physically_qualified\":false,\"external_qualified\":false}\n";
}
