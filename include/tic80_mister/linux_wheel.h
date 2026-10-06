#ifndef TIC80_MISTER_LINUX_WHEEL_H
#define TIC80_MISTER_LINUX_WHEEL_H
#include <stdint.h>
#include <linux/input.h>
#ifndef REL_HWHEEL_HI_RES
#define REL_HWHEEL_HI_RES 0x0c
#endif
/* One evdev device's state. Linux high-resolution units are 1/120 detent. */
typedef struct {
    int high_resolution, dropping;
    int64_t pending, remainder;
} tm_wheel_device;
int32_t tm_wheel_event(tm_wheel_device *, const struct input_event *, int active);
typedef struct tm_linux_wheel tm_linux_wheel;
/* No grabs, threads or subprocesses. NULL directory selects /dev/input. */
tm_linux_wheel *tm_linux_wheel_open(const char *directory);
/* epoch changes at both OSD edges, even if an entire open/close occurred
 * between polls. Inactive polls drain events and discard fractional motion. */
uint32_t tm_linux_wheel_poll(tm_linux_wheel *, uint64_t now_ms, int active, unsigned epoch);
void tm_linux_wheel_close(tm_linux_wheel *);
#endif
