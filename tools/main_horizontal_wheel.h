/* Per-evdev-device horizontal wheel normalization for pinned Main. Linux
 * high-resolution wheel reports use 120 units per detent. A capable device's
 * coarse companion reports must always be ignored, even across SYN frames. */
#ifndef TIC80_MAIN_HORIZONTAL_WHEEL_H
#define TIC80_MAIN_HORIZONTAL_WHEEL_H
#include <stdint.h>
#include <limits.h>
#include <linux/input.h>
#ifndef REL_HWHEEL_HI_RES
#define REL_HWHEEL_HI_RES 0x0c
#endif
#define TM_MAIN_HWHEEL_COMMAND 0x45
typedef struct {
    int high_resolution, dropping;
    int64_t pending, remainder;
} tm_main_horizontal_wheel;
static inline void tm_main_horizontal_clear(tm_main_horizontal_wheel *s)
{
    s->pending = s->remainder = 0;
    s->dropping = 0;
}
static inline int32_t tm_main_horizontal_event(tm_main_horizontal_wheel *s,
    unsigned type, unsigned code, int32_t value, int active)
{
    if (!active) { tm_main_horizontal_clear(s); return 0; }
    if (type == EV_SYN && code == SYN_DROPPED) {
        tm_main_horizontal_clear(s); s->dropping = 1; return 0;
    }
    if (s->dropping) {
        if (type == EV_SYN && code == SYN_REPORT) s->dropping = 0;
        return 0;
    }
    if (type == EV_REL && code == (s->high_resolution ? REL_HWHEEL_HI_RES : REL_HWHEEL)) {
        /* Bound pathological input before addition; normal evdev batches are
         * tiny. This also makes the eventual 32-bit SPI delta well-defined. */
        const int64_t limit = (int64_t)INT32_MAX * 120;
        s->pending += value;
        if (s->pending > limit) s->pending = limit;
        if (s->pending < -limit) s->pending = -limit;
    }
    if (type != EV_SYN || code != SYN_REPORT) return 0;
    int64_t total = s->pending + s->remainder;
    s->pending = 0;
    int64_t delta = s->high_resolution ? total / 120 : total;
    s->remainder = s->high_resolution ? total % 120 : 0;
    return delta > INT32_MAX ? INT32_MAX : delta < INT32_MIN ? INT32_MIN : (int32_t)delta;
}
#endif
