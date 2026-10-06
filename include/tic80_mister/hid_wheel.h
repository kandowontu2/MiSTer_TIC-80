#ifndef TIC80_MISTER_HID_WHEEL_H
#define TIC80_MISTER_HID_WHEEL_H
#include <stdint.h>
typedef struct tm_hid_wheel tm_hid_wheel;
/* The frontend control thread owns this object. Discovery runs in a separate
 * instance of this executable. Per-device helpers own all raw opens, queries,
 * reads and releases; none run in the audio loop or discovery supervisor.
 * NULL directory selects /dev. */
tm_hid_wheel *tm_hid_wheel_open(const char *directory);
uint32_t tm_hid_wheel_poll(tm_hid_wheel *, uint64_t now_ms, int active, unsigned epoch);
/* Last observed devices which configured and acknowledged the current gate.
 * Primarily useful for transport diagnostics; this performs no device I/O. */
unsigned tm_hid_wheel_devices_ready(const tm_hid_wheel *);
void tm_hid_wheel_close(tm_hid_wheel *);
/* Dispatch before normal frontend initialization for --hid-wheel-worker. */
int tm_hid_wheel_worker(int argc, char **argv);
#endif
