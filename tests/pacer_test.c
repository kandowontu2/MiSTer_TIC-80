#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/pacer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent DAC model: continuous sample clock, two-tick priming, and
 * bounded render/scheduler jitter. Advance virtual time instead of sleeping. */
typedef struct {
    uint64_t now, phase, consumed, underruns;
    uint32_t written, played, rate;
    int running;
} dac;

static void advance(dac *d, uint64_t ns)
{
    d->now += ns;
    if (!d->running) return;
    d->phase += ns * d->rate;
    uint64_t slots = d->phase / 1000000000ULL;
    d->phase %= 1000000000ULL;
    uint32_t queued = d->written - d->played;
    uint64_t taken = slots < queued ? slots : queued;
    d->underruns += slots - taken;
    d->played += (uint32_t)taken;
    d->consumed += taken;
}

static void simulation(uint32_t rate, int wrap, unsigned target)
{
    tm_pacer p;
    tm_pacer_init(&p);
    p.target_frames=target;
    dac d = {.now = 1000000000ULL, .rate = rate};
    if (wrap) {
        d.written = UINT32_MAX - 999 + target;
        d.played = UINT32_MAX - 999;
        d.running = p.audio_clocked = 1;
    }
    uint64_t started = d.now, initial = d.consumed;
    uint32_t minimum = UINT32_MAX, maximum = 0;
    uint32_t steady_minimum = UINT32_MAX;
    unsigned frames = wrap ? 2000 : 60 * 60 * 6 * 60;
    for (unsigned n = 0; n < frames; ++n) {
        /* Up to 4 ms rendering plus an occasional 18 ms scheduling pause. */
        advance(&d, 1000000ULL + (n * 7919U % 3000000U));
        if (n && n % 10007 == 0) advance(&d, 18000000ULL);
        d.written += 800;
        if (d.written - d.played >= 1600) d.running = 1;
        tm_pacer_frame(&p, d.now);
        unsigned polls = 0;
        for (;;) {
            int64_t delay = tm_pacer_delay(&p, d.written, d.played, d.now, 4096);
            assert(delay >= 0);
            if (!delay) break;
            uint64_t sleep = delay > 1000000 ? 1000000 : (uint64_t)delay;
            /* Linux wakes after a requested delay; vary that scheduling lag. */
            advance(&d, sleep + ((n * 3571U + polls++ * 127U) % 700000U));
        }
        uint32_t queued = d.written - d.played;
        if (n > 2) {
            if (queued < minimum) minimum = queued;
            if (queued > maximum) maximum = queued;
            // The larger queue needs more priming frames before its steady
            // bound applies; startup still participates in PCM/underrun checks.
            if (n > target/800 && n % 10007 && queued < steady_minimum) steady_minimum = queued;
        }
        assert(queued <= target + 800);
    }
    printf("%u Hz%s: %u ticks, queue %u..%u, underruns %llu, 18 ms scheduler pauses\n",
           rate, wrap ? " across counter wrap" : " over six hours", frames, minimum, maximum,
           (unsigned long long)d.underruns);
    assert(p.audio_clocked && !d.underruns);
    assert(minimum >= target-400 && maximum <= target);
    assert(steady_minimum >= target - 49);
    /* All produced PCM remains either played or queued, including wrap. */
    assert(d.consumed - initial + (d.written - d.played) ==
           (uint64_t)frames * 800 + (wrap ? target : 0));
    uint64_t elapsed = d.now - started;
    if (!wrap) {
        uint64_t ideal = (uint64_t)frames * 800 * 1000000000ULL / rate;
        uint64_t error = elapsed > ideal ? elapsed - ideal : ideal - elapsed;
        assert(error < 50000000ULL);
    }
    printf("%u Hz%s: %u ticks, queue %u..%u, no lost PCM or underruns\n",
           rate, wrap ? " across counter wrap" : " over six hours", frames, minimum, maximum);
}

static void fixed_clock_negative(uint32_t rate)
{
    dac d = {.rate = rate, .written = 1600, .running = 1};
    for (unsigned n = 0; n < 60000; ++n) {
        advance(&d, 1000000000ULL / 60);
        d.written += 800;
    }
    if (rate < 48000) assert(d.written - d.played > 20000);
    else assert(d.underruns > 20000);
}
static void work_budget_simulation(uint32_t rate,int wrap)
{
    tm_pacer p={.audio_clocked=1};
    dac d={.rate=rate,.written=1600,.running=1};
    if(wrap) { d.played=UINT32_MAX-999; d.written=d.played+1600; }
    // A worker stays pending: spend the entire allowed wait, render with
    // varying 1..5 ms cost, then supply a fresh 800-frame fallback block.
    for(unsigned tick=0;tick<10000;++tick) {
        int budget=tm_pacer_work_budget(&p,d.written,d.played,4096,800,30);
        assert(budget>=0 && budget<=30);
        advance(&d,(uint64_t)budget*1000000+1000000+(tick*7919U%4000000));
        assert(!d.underruns);
        d.written+=800;
        assert(d.written-d.played<=4096);
    }
    printf("Product wait budget: %u Hz%s, 10000 pending ticks, zero DAC underruns\n",rate,wrap?" across wrap":"");
}
static uint64_t checkpoint_simulation(unsigned reserve,unsigned target,unsigned maximum)
{
    tm_pacer p={.audio_clocked=1,.target_frames=target};
    dac d={.rate=48000,.written=target,.running=1};
    for(unsigned tick=0;tick<1000;++tick) {
        int budget=tm_pacer_work_budget(&p,d.written,d.played,4096,reserve,maximum);
        assert(budget>=0);
        // Most replies copy a screen; an occasional recovery reply also
        // copies a cartridge checkpoint. Its cost is independent of the
        // time spent awaiting the worker before the reply arrives.
        advance(&d,(uint64_t)budget*1000000+(tick%101==100?20000000:2000000));
        d.written+=800;
        tm_pacer_frame(&p,d.now);
        int64_t delay=tm_pacer_delay(&p,d.written,d.played,d.now,4096);
        assert(delay>=0);
        advance(&d,(uint64_t)delay);
    }
    return d.underruns;
}

int main(void)
{
    tm_pacer p;
    tm_pacer_init(&p);
    tm_pacer_frame(&p, 1000000000ULL);
    assert(tm_pacer_delay(&p, 800, 0, 1000000000ULL, 4096) == 1000000000ULL / 60);
    assert(tm_pacer_delay(&p, 800, 0, p.startup_deadline_ns, 4096) == 0);
    assert(!p.audio_clocked);
    assert(tm_pacer_delay(&p, 1, 2, 0, 4096) == -1);
    assert(tm_pacer_delay(&p, 4097, 0, 0, 4096) == -1);
    p.audio_clocked = 1;
    assert(tm_pacer_delay(&p, 800, 0, 0, 4096) == 0); /* played wrapped to zero */
    tm_pacer_init(&p);
    assert(!p.audio_clocked && !p.startup_deadline_ns);
    assert(tm_pacer_work_budget(&p,800,0,4096,800,30)==30);
    assert(tm_pacer_work_budget(&p,1600,0,4096,1200,40)==8);
    assert(tm_pacer_work_budget(&p,1,2,4096,800,30)==-1);
    p.audio_clocked=1;
    assert(tm_pacer_work_budget(&p,800,0,4096,800,30)==0);
    assert(tm_pacer_work_budget(&p,1600,0,4096,800,30)==16);
    p.target_frames=2400;
    assert(tm_pacer_work_budget(&p,2400,0,4096,1600,30)==16);
    assert(tm_pacer_delay(&p,3200,0,0,4096)==16666667);
    assert(tm_pacer_delay(&p,2400,0,0,4096)==0);
    p.target_frames=4097;
    assert(tm_pacer_delay(&p,2400,0,0,4096)==-1);
    work_budget_simulation(47976,0);
    work_budget_simulation(48000,0);
    work_budget_simulation(48024,0);
    work_budget_simulation(48000,1);
    assert(checkpoint_simulation(TM_AUDIO_RESERVE_FRAMES,1600,30)==0);
    assert(checkpoint_simulation(TM_AUDIO_RESERVE_FRAMES,2400,30)==0);
    assert(checkpoint_simulation(1200,2400,30)==0);
    assert(checkpoint_simulation(TM_AUDIO_RESERVE_FRAMES,3200,30)==0);
    assert(checkpoint_simulation(1200,3200,40)==0);
    assert(checkpoint_simulation(800,1600,30)>0);
    puts("Recovery checkpoint model: 25 ms copy reserve passes; one-tick policy underruns");
    simulation(47976, 0,1600);
    simulation(48000, 0,1600);
    simulation(48024, 0,1600);
    simulation(48000, 1,1600);
    simulation(47976, 0,2400);
    simulation(48000, 0,2400);
    simulation(48024, 0,2400);
    simulation(48000, 1,2400);
    simulation(47976, 0,3200);
    simulation(48000, 0,3200);
    simulation(48024, 0,3200);
    simulation(48000, 1,3200);
    fixed_clock_negative(47976);
    fixed_clock_negative(48024);
    puts("Audio pacing: startup, reset, wrap, +/-500 ppm and fixed-clock negative controls passed");
    return 0;
}
