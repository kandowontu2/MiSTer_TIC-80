#ifndef TIC80_MISTER_PACER_H
#define TIC80_MISTER_PACER_H
#include <stdint.h>

enum { TM_AUDIO_RESERVE_FRAMES = 1600 };

typedef struct {
    uint64_t startup_deadline_ns;
    int audio_clocked;
    /* Zero selects the default two-tick queue; Studio needs worker credit. */
    unsigned target_frames;
} tm_pacer;

void tm_pacer_init(tm_pacer *pacer);
void tm_pacer_frame(tm_pacer *pacer, uint64_t now_ns);
/* Return delay before generating the next tick, zero when ready, or -1 for
 * an invalid ring occupancy. Unsigned counter differences support wrap. */
int64_t tm_pacer_delay(tm_pacer *pacer, uint32_t written, uint32_t played,
                       uint64_t now_ns, uint32_t capacity);
/* Budget for awaiting a product while retaining reserve samples for copying
 * and submission. Before playback starts, allow bounded priming work. */
int tm_pacer_work_budget(const tm_pacer *pacer, uint32_t written, uint32_t played,
                         uint32_t capacity, unsigned reserve, unsigned maximum_ms);
#endif
