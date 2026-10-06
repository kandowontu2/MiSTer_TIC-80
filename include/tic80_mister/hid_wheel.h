#ifndef TIC80_MISTER_HID_WHEEL_H
#define TIC80_MISTER_HID_WHEEL_H
#include <stdint.h>
typedef struct tm_hid_wheel tm_hid_wheel;
/* The frontend control thread owns this object. Device discovery and feature
 * GET requests run in a separate instance of this executable, never in the
 * audio publication loop. NULL directory selects /dev. */
tm_hid_wheel *tm_hid_wheel_open(const char *directory);
uint32_t tm_hid_wheel_poll(tm_hid_wheel *, uint64_t now_ms, int active, unsigned epoch);
void tm_hid_wheel_close(tm_hid_wheel *);
/* Dispatch before normal frontend initialization for --hid-wheel-worker. */
int tm_hid_wheel_worker(int argc, char **argv);
#endif
