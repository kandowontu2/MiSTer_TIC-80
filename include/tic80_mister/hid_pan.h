#ifndef TIC80_MISTER_HID_PAN_H
#define TIC80_MISTER_HID_PAN_H
#include <stddef.h>
#include <stdint.h>
enum { TM_HID_PAN_FIELDS = 32, TM_HID_COLLECTIONS = 128, TM_HID_REPORT_BYTES = 4096 };
typedef struct {
    uint32_t offset, width, flags;
    int64_t minimum, maximum;
    int collection, multiplier;
    uint8_t report;
    int64_t fraction;
} tm_hid_pan_field;
typedef struct {
    uint32_t offset, width;
    int64_t minimum, maximum, physical_minimum, physical_maximum;
    int collection, effective, ready;
    uint8_t report;
} tm_hid_pan_multiplier;
typedef struct {
    unsigned count, multipliers, collections;
    int numbered;
    uint32_t input_bits[256], feature_bits[256];
    int parent[TM_HID_COLLECTIONS];
    uint8_t collection_type[TM_HID_COLLECTIONS];
    tm_hid_pan_field field[TM_HID_PAN_FIELDS];
    tm_hid_pan_multiplier multiplier[TM_HID_PAN_FIELDS];
} tm_hid_pan;
/* 1 = relative Consumer AC Pan found, 0 = no pan, -1 = malformed/unsupported
 * descriptor. Failure clears all output state. No device I/O occurs here. */
int tm_hid_pan_parse(tm_hid_pan *, const uint8_t *, size_t);
/* Feature payload contains field bytes only. The transport handles any report
 * ID prefix. Read current values; never change a device's multiplier. */
int tm_hid_pan_feature(tm_hid_pan *, unsigned report, const uint8_t *, size_t);
int tm_hid_pan_ready(const tm_hid_pan *);
/* Whether any pan field depends on this feature report. */
int tm_hid_pan_feature_required(const tm_hid_pan *, unsigned report);
/* Input payload includes an ID only when the descriptor declares numbered
 * reports. Return 1 for decoded pan, 0 for an unrelated report, -1 for a short
 * or invalid report. Failure discards partial detents; output is always set. */
int tm_hid_pan_input(tm_hid_pan *, const uint8_t *, size_t, int64_t *detents);
void tm_hid_pan_clear(tm_hid_pan *);
#endif
