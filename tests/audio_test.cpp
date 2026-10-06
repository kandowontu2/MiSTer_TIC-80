#include "Vtic80_audio.h"
#include "tic80_mister/memory_map.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static uint32_t sample(unsigned index) { return ((uint32_t)(uint16_t)~(index + 1) << 16) | (uint16_t)(index + 1); }
struct Sim {
    Vtic80_audio dut;
    unsigned cycle = 0, audio_phase = 0, response_delay = 0, received = 0;
    uint64_t response = 0;
    bool awaiting = false, run_audio = true, check_samples = false;
    unsigned expected = TM_AUDIO_CAPACITY;
    uint32_t previous_sample = 0;
    void step() {
        dut.clk_sys = 0; dut.clk_audio = 0;
        dut.ready = awaiting && !response_delay;
        dut.data = response;
        dut.eval();
        bool accepted = dut.ready;
        if (!awaiting && dut.request) {
            unsigned index = (dut.address - ((TM_PHYSICAL_BASE + TM_AUDIO_RING_OFFSET) >> 3)) * 2;
            CHECK(index < TM_AUDIO_CAPACITY);
            response = (uint64_t)sample(index) | ((uint64_t)sample(index + 1) << 32);
            response_delay = 3 + cycle % 9;
            awaiting = true;
        }
        if (response_delay) --response_delay;
        if (accepted) awaiting = false;
        audio_phase += run_audio ? 24576 : 0;
        bool audio_edge = audio_phase >= 105000;
        if (audio_edge) audio_phase -= 105000;
        dut.clk_sys = 1;
        dut.clk_audio = audio_edge;
        dut.eval();
        uint32_t pcm = dut.audio_l | ((uint32_t)dut.audio_r << 16);
        if (check_samples && pcm != previous_sample) {
            if (pcm) { CHECK(pcm == sample(received)); ++received; }
            else CHECK(received == expected);
        }
        previous_sample = pcm;
        ++cycle;
    }
    void steps(unsigned n) { while (n--) step(); }
    void start() {
        dut.reset = 1; dut.session_reset = 0; dut.session_active = 0;
        dut.write_counter = 0; steps(50); dut.reset = 0; steps(50);
        dut.session_active = 1;
    }
    void drain() {
        unsigned limit = cycle + (expected + 2500) * 2200;
        while ((received < expected || previous_sample) && cycle < limit) step();
        CHECK(received == expected);
        steps(20);
        CHECK(dut.read_counter == expected);
    }
};
int main() {
    Sim s;
    s.dut.reset = 1; s.dut.session_reset = 0; s.dut.session_active = 0;
    s.dut.write_counter = 0; s.steps(50); s.dut.reset = 0; s.steps(50);
    s.dut.session_active = 1;
    s.dut.write_counter = 1;
    s.steps(500);
    CHECK(!s.dut.request && s.dut.read_counter == 0); // unpublished second sample
    CHECK(!s.dut.sample_counter && !s.dut.underrun_counter); // startup silence is excluded
    s.run_audio = false;
    s.dut.write_counter = TM_AUDIO_CAPACITY;
    s.steps(5000);
    CHECK(s.dut.read_counter == 0 && !s.dut.request); // full FIFO; no playback ACK yet
    CHECK(!s.dut.sample_counter && !s.dut.underrun_counter);
    s.run_audio = true;
    s.check_samples = true;
    unsigned limit = s.cycle + TM_AUDIO_CAPACITY * 512 * 5;
    while ((s.received < TM_AUDIO_CAPACITY || s.previous_sample) && s.cycle < limit) s.step();
    CHECK(s.received == TM_AUDIO_CAPACITY);
    s.steps(20); // Gray playback counter crosses back to the producer clock
    CHECK(s.dut.read_counter == TM_AUDIO_CAPACITY);
    CHECK(!s.dut.audio_l && !s.dut.audio_r); // underrun silence, no counter advance
    s.steps(10000);
    CHECK(s.dut.read_counter == TM_AUDIO_CAPACITY);
    CHECK(s.dut.underrun_counter > 0);
    CHECK(s.dut.sample_counter - s.dut.underrun_counter == TM_AUDIO_CAPACITY);
    unsigned before=s.dut.sample_counter, gaps=s.dut.underrun_counter;
    s.steps(105000); // one millisecond at the independent producer clock
    CHECK(s.dut.sample_counter-before>=47 && s.dut.sample_counter-before<=49);
    CHECK(s.dut.underrun_counter-gaps == s.dut.sample_counter-before);
    s.check_samples = false;
    s.dut.session_reset = 1; s.steps(50); s.dut.session_reset = 0;
    s.dut.write_counter = 0; s.steps(50);
    CHECK(!s.dut.audio_l && !s.dut.audio_r && s.dut.read_counter == 0);
    CHECK(!s.dut.request);
    CHECK(!s.dut.sample_counter && !s.dut.underrun_counter);
    {
        Sim jitter;
        jitter.start(); jitter.expected = 2400; jitter.check_samples = true;
        jitter.dut.write_counter = 800;
        jitter.steps(40 * 105000); // slow second tick: hold first block intact
        CHECK(!jitter.received && !jitter.dut.sample_counter && !jitter.dut.underrun_counter);
        jitter.dut.write_counter = 1600;
        jitter.steps(105000); // threshold reached: start without 50 ms timeout
        CHECK(jitter.received >= 47 && jitter.received <= 49);
        jitter.steps(24 * 105000); // third tick is late, but the buffer covers it
        CHECK(!jitter.dut.underrun_counter);
        jitter.dut.write_counter = 2400;
        jitter.drain();
        CHECK(jitter.dut.underrun_counter <= 1); // at most the terminal empty slot
        CHECK(jitter.dut.sample_counter - jitter.dut.underrun_counter == 2400);
    }
    for (unsigned short_frames : {2u, 800u}) {
        Sim clip;
        clip.start(); clip.expected = short_frames; clip.check_samples = true;
        clip.dut.write_counter = short_frames;
        clip.steps(49 * 105000);
        CHECK(!clip.received && !clip.dut.sample_counter && !clip.dut.underrun_counter);
        clip.drain(); // bounded fallback preserves and drains every sample
        CHECK(clip.cycle < (51 + 17) * 105000);
    }
    {
        Sim restart;
        restart.start(); restart.dut.write_counter = 800;
        restart.steps(40 * 105000);
        restart.dut.session_reset = 1; restart.steps(50);
        restart.dut.write_counter = 0; restart.dut.session_reset = 0; restart.steps(50);
        restart.expected = 800; restart.check_samples = true;
        restart.dut.write_counter = 800; restart.steps(49 * 105000);
        CHECK(!restart.received && !restart.dut.sample_counter);
        restart.drain();
    }
    std::puts("48 kHz divider, stereo order, FIFO full, unpublished pair, 4096 samples and reset verified");
    std::puts("Two-tick startup buffering covers delayed production; short clips drain within 50 ms + clip duration");
}
