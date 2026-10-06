#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../tools/main_horizontal_wheel.h"
#include <assert.h>
#include <stdio.h>
static int32_t report(tm_main_horizontal_wheel *s)
{ return tm_main_horizontal_event(s, EV_SYN, SYN_REPORT, 0, 1); }
static void relative(tm_main_horizontal_wheel *s, unsigned code, int32_t value)
{ assert(!tm_main_horizontal_event(s, EV_REL, code, value, 1)); }
int main(void)
{
    tm_main_horizontal_wheel coarse = {0}, fine = {.high_resolution = 1};
    relative(&coarse, REL_HWHEEL, 3); relative(&coarse, REL_HWHEEL, -1);
    relative(&coarse, REL_WHEEL, 7); assert(report(&coarse) == 2);
    assert(report(&coarse) == 0);
    relative(&coarse, REL_HWHEEL, INT32_MIN); assert(report(&coarse) == INT32_MIN);
    relative(&coarse, REL_HWHEEL, INT32_MAX); relative(&coarse, REL_HWHEEL, 99);
    assert(report(&coarse) == INT32_MAX);
    // High-resolution companion events need not occur in the same frame.
    relative(&fine, REL_HWHEEL_HI_RES, 30); assert(report(&fine) == 0);
    relative(&fine, REL_HWHEEL, 1); assert(report(&fine) == 0);
    relative(&fine, REL_HWHEEL_HI_RES, 90); assert(report(&fine) == 1);
    relative(&fine, REL_HWHEEL_HI_RES, -250); assert(report(&fine) == -2);
    relative(&fine, REL_HWHEEL_HI_RES, -110); assert(report(&fine) == -1);
    relative(&fine, REL_HWHEEL_HI_RES, 60); assert(report(&fine) == 0);
    relative(&coarse, REL_HWHEEL, -5); assert(report(&coarse) == -5);
    relative(&fine, REL_HWHEEL_HI_RES, 60); assert(report(&fine) == 1);
    // Lost events, OSD/core deselection and disconnect discard partial input.
    relative(&fine, REL_HWHEEL_HI_RES, 60); assert(report(&fine) == 0);
    relative(&fine, REL_HWHEEL_HI_RES, 500);
    assert(!tm_main_horizontal_event(&fine, EV_SYN, SYN_DROPPED, 0, 1));
    relative(&fine, REL_HWHEEL_HI_RES, 999); assert(report(&fine) == 0);
    relative(&fine, REL_HWHEEL_HI_RES, 60); assert(report(&fine) == 0);
    assert(!tm_main_horizontal_event(&fine, EV_REL, REL_HWHEEL_HI_RES, 120, 0));
    relative(&fine, REL_HWHEEL_HI_RES, 60); assert(report(&fine) == 0);
    tm_main_horizontal_clear(&fine);
    relative(&fine, REL_HWHEEL_HI_RES, 120); assert(report(&fine) == 1);
    puts("Main horizontal wheel: per-device coarse/high-resolution, companion suppression, signs, loss and mode gating passed");
}
