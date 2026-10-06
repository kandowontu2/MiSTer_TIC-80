#include "tic80_mister/hid_pan.h"
#include <limits.h>
#include <string.h>

typedef struct {
    uint32_t page, size, count, report;
    int64_t minimum, maximum, physical_minimum, physical_maximum;
} globals;
typedef struct { uint32_t first, last; } usage_span;
typedef struct {
    usage_span span[128]; unsigned count;
    uint32_t minimum; int range_pending, delimiter_open; unsigned branches;
} locals;
static uint32_t little(const uint8_t *p, unsigned bytes)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < bytes; ++i) v |= (uint32_t)p[i] << (8*i);
    return v;
}
static int32_t sign(uint32_t v, unsigned bits)
{
    if (!bits) return 0;
    if (bits < 32 && (v & (1u << (bits-1)))) return (int32_t)((int64_t)v - (1LL << bits));
    return v <= INT32_MAX ? (int32_t)v : (int32_t)((int64_t)v - 4294967296LL);
}
static uint32_t usage(const locals *s, unsigned n)
{
    for (unsigned i = 0; i < s->count; ++i) {
        uint64_t count = (uint64_t)s->span[i].last - s->span[i].first + 1;
        if (n < count) return s->span[i].first + n;
        n -= (unsigned)count;
    }
    return s->count ? s->span[s->count-1].last : 0;
}
static int has_usage(const locals *s, uint32_t value)
{
    for (unsigned i = 0; i < s->count; ++i)
        if (s->span[i].first <= value && value <= s->span[i].last) return 1;
    return 0;
}
static int ancestor(const tm_hid_pan *s, int scope, int child)
{
    if (scope < 0) return 1;
    while (child >= 0) {
        if (scope == child) return 1;
        child = s->parent[child];
    }
    return 0;
}
static int logical(const tm_hid_pan *s, int collection)
{
    while (collection >= 0) {
        if (s->collection_type[collection] == 2) return collection;
        collection = s->parent[collection];
    }
    return -1;
}
int tm_hid_pan_parse(tm_hid_pan *s, const uint8_t *data, size_t size)
{
    globals g = {0}, stack[16]; unsigned pushed = 0;
    locals l = {0}; int collection = -1;
    uint32_t output_bits[256] = {0};
    unsigned feature_order[256], next_feature = 0;
    memset(s, 0, sizeof *s);
    for (unsigned i = 0; i < 256; ++i) feature_order[i] = UINT_MAX;
    if (!data || !size || size > TM_HID_REPORT_BYTES) goto invalid;
    for (size_t cursor = 0; cursor < size;) {
        uint8_t prefix = data[cursor++];
        if (prefix == 0xfe) { /* Unknown long items cannot define a pan field. */
            if (size-cursor < 2) goto invalid;
            unsigned length = data[cursor]; cursor += 2;
            if (size-cursor < length) goto invalid;
            cursor += length; continue;
        }
        unsigned bytes = prefix & 3; if (bytes == 3) bytes = 4;
        unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (size-cursor < bytes) goto invalid;
        uint32_t v = little(data+cursor, bytes); cursor += bytes;
        int32_t signed_v = sign(v, bytes*8);
        if (type == 1) {
            switch (tag) {
            case 0: if (v > 65535) goto invalid; g.page = v; break;
            case 1: g.minimum = signed_v; break;
            case 2: g.maximum = g.minimum < 0 ? (int64_t)signed_v : (int64_t)v; break;
            case 3: g.physical_minimum = signed_v; break;
            case 4: g.physical_maximum = g.physical_minimum < 0 ? (int64_t)signed_v : (int64_t)v; break;
            case 7: g.size = v; break;
            case 8: if (!v || v > 255) goto invalid; g.report = v; break;
            case 9: g.count = v; break;
            case 10: if (pushed == 16) goto invalid; stack[pushed++] = g; break;
            case 11: if (!pushed) goto invalid; g = stack[--pushed]; break;
            default: break; /* Unit and exponent do not affect Linux wheel scaling. */
            }
        } else if (type == 2) {
            uint32_t u = bytes == 4 ? v : (g.page << 16) | v;
            if (tag == 10) {
                if (v) {
                    if (l.delimiter_open || l.range_pending) goto invalid;
                    l.delimiter_open = 1; ++l.branches;
                } else {
                    if (!l.delimiter_open || l.range_pending) goto invalid;
                    l.delimiter_open = 0;
                }
            } else if (l.branches > 1) {
                /* Linux resolves common usages and the first alternative. */
            } else if (tag == 0) {
                if (l.count == 128) goto invalid;
                l.span[l.count++] = (usage_span){u,u};
            } else if (tag == 1) {
                if (l.range_pending) goto invalid;
                l.minimum = u; l.range_pending = 1;
            } else if (tag == 2) {
                if (!l.range_pending || u < l.minimum || (u >> 16) != (l.minimum >> 16) || l.count == 128) goto invalid;
                l.span[l.count++] = (usage_span){l.minimum,u}; l.range_pending = 0;
            }
        } else if (type == 0) {
            if (l.range_pending || l.delimiter_open) goto invalid;
            if (tag == 10) {
                if (s->collections == TM_HID_COLLECTIONS || v > 255) goto invalid;
                unsigned n = s->collections++;
                s->parent[n] = collection; s->collection_type[n] = v; collection = (int)n;
            } else if (tag == 12) {
                if (collection < 0) goto invalid;
                collection = s->parent[collection];
            } else if (tag == 8 || tag == 9 || tag == 11) {
                uint32_t *offsets = tag == 8 ? s->input_bits : tag == 9 ? output_bits : s->feature_bits;
                uint64_t length = (uint64_t)g.size * g.count;
                if (g.maximum < g.minimum || (!g.size && g.count)) goto invalid;
                if (length > TM_HID_REPORT_BYTES*8 || offsets[g.report]+length > TM_HID_REPORT_BYTES*8) goto invalid;
                if (tag == 8 && g.report && length) s->numbered = 1;
                if (tag == 11 && feature_order[g.report] == UINT_MAX) feature_order[g.report] = next_feature++;
                for (unsigned n = 0; n < g.count && g.size && l.count; ++n) {
                    uint32_t u = usage(&l,n);
                    if (tag == 8 && u == 0x000c0238 && (v & 7) == 6) {
                        if (s->count == TM_HID_PAN_FIELDS || g.size > 32) goto invalid;
                        tm_hid_pan_field *f = &s->field[s->count++];
                        f->offset = offsets[g.report]+n*g.size; f->width = g.size; f->flags = v;
                        f->minimum = g.minimum; f->maximum = g.maximum;
                        f->collection = collection; f->multiplier = 1; f->report = g.report;
                    }
                }
                /* The kernel uses the field's first feature value even if
                 * its Resolution Multiplier usage has another array index.
                 * Do not assume the kernel set multi-value fields to maximum. */
                if (tag == 11 && g.count && has_usage(&l,0x00010048)) {
                    if (s->multipliers == TM_HID_PAN_FIELDS || !g.size || g.size > 32) goto invalid;
                    tm_hid_pan_multiplier *m = &s->multiplier[s->multipliers++];
                    m->offset = offsets[g.report]; m->width = g.size;
                    m->minimum = g.minimum; m->maximum = g.maximum;
                    m->physical_minimum = g.physical_minimum; m->physical_maximum = g.physical_maximum;
                    m->collection = logical(s,collection); m->effective = 1; m->report = g.report;
                }
                offsets[g.report] += (uint32_t)length;
            }
            memset(&l, 0, sizeof l);
        }
    }
    if (collection >= 0 || pushed || l.range_pending || l.delimiter_open || (s->numbered && s->input_bits[0])) goto invalid;
    /* Match kernel feature-report iteration: report registration order, then
     * each field within that report. Last applicable multiplier wins. */
    for (unsigned n = 1; n < s->multipliers; ++n) {
        tm_hid_pan_multiplier m = s->multiplier[n]; unsigned i = n;
        while (i && feature_order[s->multiplier[i-1].report] > feature_order[m.report]) {
            s->multiplier[i] = s->multiplier[i-1]; --i;
        }
        s->multiplier[i] = m;
    }
    return s->count ? 1 : 0;
invalid:
    memset(s, 0, sizeof *s); return -1;
}
static uint32_t bits(const uint8_t *data, uint32_t offset, unsigned width)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < width; ++i) v |= ((data[(offset+i)/8] >> ((offset+i)%8)) & 1u) << i;
    return v;
}
void tm_hid_pan_clear(tm_hid_pan *s)
{
    for (unsigned n = 0; n < s->count; ++n) s->field[n].fraction = 0;
}
int tm_hid_pan_feature(tm_hid_pan *s, unsigned report, const uint8_t *data, size_t size)
{
    if (report > 255 || !data || size < (s->feature_bits[report]+7)/8) return -1;
    /* Validate the complete response before changing cached scales. Bounding
     * feature values also keeps malformed 32-bit descriptors from overflowing
     * the effective-multiplier arithmetic. */
    for (unsigned n = 0; n < s->multipliers; ++n) {
        const tm_hid_pan_multiplier *m = &s->multiplier[n];
        if (m->report != report) continue;
        uint32_t raw = bits(data,m->offset,m->width);
        int64_t value = m->minimum < 0 ? (int64_t)sign(raw,m->width) : (int64_t)raw;
        if (value < m->minimum || value > m->maximum) return -1;
    }
    int found = 0;
    for (unsigned n = 0; n < s->multipliers; ++n) {
        tm_hid_pan_multiplier *m = &s->multiplier[n];
        if (m->report != report) continue;
        uint32_t raw = bits(data,m->offset,m->width);
        int64_t value = m->minimum < 0 ? (int64_t)sign(raw,m->width) : (int64_t)raw;
        int64_t range = (int64_t)m->maximum-m->minimum;
        int64_t effective = range ? (value-m->minimum)/range * ((int64_t)m->physical_maximum-m->physical_minimum)+m->physical_minimum : 1;
        m->effective = effective && effective >= -255 && effective <= 255 ? (int)effective : 1;
        m->ready = 1; found = 1;
    }
    for (unsigned n = 0; n < s->count; ++n) {
        tm_hid_pan_field *f = &s->field[n]; int multiplier = 1;
        for (unsigned j = 0; j < s->multipliers; ++j)
            if (s->multiplier[j].ready && ancestor(s,s->multiplier[j].collection,f->collection)) multiplier = s->multiplier[j].effective;
        if (multiplier != f->multiplier) f->fraction = 0;
        f->multiplier = multiplier;
    }
    return found;
}
int tm_hid_pan_ready(const tm_hid_pan *s)
{
    for (unsigned n = 0; n < s->multipliers; ++n) {
        const tm_hid_pan_multiplier *m = &s->multiplier[n];
        if (m->ready) continue;
        for (unsigned j = 0; j < s->count; ++j) if (ancestor(s,m->collection,s->field[j].collection)) return 0;
    }
    return 1;
}
int tm_hid_pan_feature_required(const tm_hid_pan *s, unsigned report)
{
    for (unsigned n = 0; n < s->multipliers; ++n) {
        const tm_hid_pan_multiplier *m = &s->multiplier[n];
        if (m->report != report) continue;
        for (unsigned j = 0; j < s->count; ++j)
            if (ancestor(s,m->collection,s->field[j].collection)) return 1;
    }
    return 0;
}
int tm_hid_pan_input(tm_hid_pan *s, const uint8_t *data, size_t size, int64_t *detents)
{
    if (!detents) return -1;
    *detents = 0;
    if (!data || !size) goto invalid;
    unsigned report = s->numbered ? *data++ : 0;
    if (s->numbered) --size;
    if (!s->input_bits[report]) return 0;
    if (size < (s->input_bits[report]+7)/8) goto invalid;
    if (!tm_hid_pan_ready(s)) return 0;
    int found = 0;
    for (unsigned n = 0; n < s->count; ++n) {
        tm_hid_pan_field *f = &s->field[n];
        if (f->report != report) continue;
        uint32_t raw = bits(data,f->offset,f->width);
        int64_t value = f->minimum < 0 ? (int64_t)sign(raw,f->width) : (int64_t)raw;
        if (f->minimum < f->maximum && (value < f->minimum || value > f->maximum)) {
            if (f->flags & 64) continue;
            value = value < f->minimum ? f->minimum : f->maximum;
        }
        /* Use widened arithmetic, then the same 120-unit convention as
         * Linux. Keep fractions per field rather than mixing devices. */
        int64_t units = value*120/f->multiplier + f->fraction;
        *detents += units/120; f->fraction = units%120; found = 1;
    }
    return found;
invalid:
    tm_hid_pan_clear(s); return -1;
}
