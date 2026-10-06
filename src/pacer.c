#include "tic80_mister/pacer.h"
#include <string.h>

void tm_pacer_init(tm_pacer *p)
{
    memset(p, 0, sizeof *p);
}

void tm_pacer_frame(tm_pacer *p, uint64_t now)
{
    if (p->audio_clocked) return;
    const uint64_t period = 1000000000ULL / 60;
    if (!p->startup_deadline_ns) p->startup_deadline_ns = now;
    p->startup_deadline_ns += period;
    if (now > p->startup_deadline_ns + period) p->startup_deadline_ns = now;
}

int64_t tm_pacer_delay(tm_pacer *p, uint32_t written, uint32_t played,
                       uint64_t now, uint32_t capacity)
{
    uint32_t queued = written - played;
    unsigned target=p->target_frames?p->target_frames:TM_AUDIO_RESERVE_FRAMES;
    if (queued > capacity || target > capacity) return -1;
    if (played) p->audio_clocked = 1;
    if (p->audio_clocked) {
        /* TIC synthesizes 800 stereo frames per tick. Generate the next tick
         * at the configured queue target, keeping time tied to playback rather
         * than accumulating the difference between two nominal 48 kHz clocks.
         * The reserve tolerates a scheduler delay beyond one tick without
         * accumulating latency. Keep the latch across a played-counter wrap. */
        return queued <= target ? 0 :
            ((uint64_t)(queued - target) * 1000000000ULL + 47999) / 48000;
    }
    return now < p->startup_deadline_ns ? (int64_t)(p->startup_deadline_ns - now) : 0;
}
int tm_pacer_work_budget(const tm_pacer *p,uint32_t written,uint32_t played,
                         uint32_t capacity,unsigned reserve,unsigned maximum_ms)
{
    uint32_t queued=written-played;
    if(!p || queued>capacity || reserve>capacity || maximum_ms>1000) return -1;
    // Two queued ticks arm the DAC before its first consumed-sample count is
    // visible. Budget that primed queue as live PCM rather than a free wait.
    if(!p->audio_clocked && !played && queued<1600) return (int)maximum_ms;
    if(queued<=reserve) return 0;
    unsigned budget=(unsigned)((uint64_t)(queued-reserve)*1000/48000);
    return (int)(budget<maximum_ms?budget:maximum_ms);
}
