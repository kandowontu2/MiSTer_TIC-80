#include "tic80_mister/hid_pan.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Fixed wire descriptor: a composite mouse report with five buttons, padding,
 * X/Y/wheel, and signed Consumer AC Pan. This is independent of the descriptor
 * builder used below to exercise optional HID fields. */
static const uint8_t mouse[] = {
    0x05,1, 0x09,2, 0xa1,1, 0x09,1, 0xa1,0,
    0x05,9, 0x19,1, 0x29,5, 0x15,0, 0x25,1,
    0x75,1, 0x95,5, 0x81,2, 0x75,3, 0x95,1, 0x81,3,
    0x05,1, 0x09,0x30, 0x09,0x31, 0x09,0x38,
    0x15,0x81, 0x25,0x7f, 0x75,8, 0x95,3, 0x81,6,
    0x05,0x0c, 0x0a,0x38,2, 0x95,1, 0x81,6, 0xc0,0xc0
};
typedef struct { uint8_t data[4096]; size_t size; } descriptor;
static void item(descriptor *d, unsigned type, unsigned tag, unsigned bytes, uint32_t v)
{
    assert(d->size + bytes + 1 <= sizeof d->data);
    d->data[d->size++] = (uint8_t)((tag << 4) | (type << 2) | (bytes == 4 ? 3 : bytes));
    for (unsigned i = 0; i < bytes; ++i) d->data[d->size++] = (uint8_t)(v >> (8*i));
}
static void global(descriptor *d, unsigned tag, unsigned bytes, uint32_t v) { item(d,1,tag,bytes,v); }
static void local(descriptor *d, unsigned tag, unsigned bytes, uint32_t v) { item(d,2,tag,bytes,v); }
static void main_item(descriptor *d, unsigned tag, unsigned bytes, uint32_t v) { item(d,0,tag,bytes,v); }
static void begin(descriptor *d, unsigned type) { main_item(d,10,1,type); }
static void end(descriptor *d) { main_item(d,12,0,0); }
static void range(descriptor *d, int32_t low, uint32_t high)
{
    global(d,1,4,(uint32_t)low); global(d,2,4,high);
}
static void pan(descriptor *d, unsigned width, int32_t low, uint32_t high, unsigned flags)
{
    global(d,0,1,12); local(d,0,2,0x238); range(d,low,high);
    global(d,7,1,width); global(d,9,1,1); main_item(d,8,1,flags);
}
static void multiplier(descriptor *d, unsigned report, int32_t low, uint32_t high,
                       int32_t plow, uint32_t phigh, unsigned width)
{
    if (report) global(d,8,1,report);
    global(d,0,1,1); local(d,0,1,0x48); range(d,low,high);
    global(d,3,4,(uint32_t)plow); global(d,4,4,phigh);
    global(d,7,1,width); global(d,9,1,1); main_item(d,11,1,2);
}
static void parse(tm_hid_pan *s, const descriptor *d) { assert(tm_hid_pan_parse(s,d->data,d->size) == 1); }
static int64_t input(tm_hid_pan *s, const uint8_t *data, size_t size, int status)
{
    int64_t delta = INT64_MAX;
    assert(tm_hid_pan_input(s,data,size,&delta) == status);
    return delta;
}
static void plain_reports(void)
{
    tm_hid_pan s;
    assert(tm_hid_pan_parse(&s,mouse,sizeof mouse) == 1);
    assert(s.count == 1 && !s.numbered && s.field[0].offset == 32 && s.input_bits[0] == 40);
    uint8_t report[] = {0x1f,127,128,1,1};
    assert(input(&s,report,sizeof report,1) == 1);
    report[4] = 0xff; assert(input(&s,report,sizeof report,1) == -1);
    report[4] = 0x80; assert(input(&s,report,sizeof report,1) == -127); /* Range clamp. */
    assert(input(&s,report,sizeof report-1,-1) == 0);
    assert(input(&s,NULL,0,-1) == 0);
    /* Absolute AC Pan, arrays, constants, and a keyboard range aren't wheels. */
    for (unsigned flags = 0; flags < 8; ++flags) {
        descriptor d = {0}; pan(&d,8,-127,127,flags);
        assert(tm_hid_pan_parse(&s,d.data,d.size) == (flags == 6 ? 1 : 0));
    }
    descriptor d = {0};
    global(&d,0,1,7); local(&d,1,1,0); local(&d,2,2,255);
    range(&d,0,1); global(&d,7,1,1); global(&d,9,2,256); main_item(&d,8,1,2);
    pan(&d,8,-127,127,6); parse(&s,&d);
    assert(s.field[0].offset == 256);
    uint8_t composite[33] = {0}; composite[32] = 0xfe;
    assert(input(&s,composite,sizeof composite,1) == -2);
}
static void numbered_and_wide(void)
{
    tm_hid_pan s; descriptor d = {0};
    global(&d,8,1,7); range(&d,0,1); global(&d,7,1,3); global(&d,9,1,1);
    main_item(&d,8,1,3); pan(&d,5,-16,15,6); parse(&s,&d);
    uint8_t report[] = {7,0xf8}; /* -1 at bit offset 3. */
    assert(s.numbered && s.field[0].offset == 3);
    assert(input(&s,report,2,1) == -1);
    report[1] = 0x78; assert(input(&s,report,2,1) == 15);
    report[0] = 6; assert(input(&s,report,2,0) == 0);
    report[0] = 7; assert(input(&s,report,1,-1) == 0);
    d = (descriptor){0}; pan(&d,32,INT32_MIN,INT32_MAX,6); parse(&s,&d);
    uint8_t minimum[] = {0,0,0,128}, maximum[] = {255,255,255,127};
    assert(input(&s,minimum,4,1) == INT32_MIN);
    assert(input(&s,maximum,4,1) == INT32_MAX);
    d = (descriptor){0}; pan(&d,32,0,UINT32_MAX,6); parse(&s,&d);
    uint8_t unsigned_max[] = {255,255,255,255};
    assert(input(&s,unsigned_max,4,1) == UINT32_MAX);
    /* Feature-only numbered reports do not number the unnumbered input. */
    d = (descriptor){0}; pan(&d,8,-127,127,6); multiplier(&d,4,0,1,1,8,8); parse(&s,&d);
    assert(!s.numbered); uint8_t feature = 1;
    assert(tm_hid_pan_feature(&s,4,&feature,1) == 1);
    uint8_t tick = 8; assert(input(&s,&tick,1,1) == 1);
}
static void scaling_and_loss(void)
{
    tm_hid_pan s; descriptor d = {0};
    begin(&d,1); begin(&d,2); multiplier(&d,0,0,1,1,8,2);
    pan(&d,8,-127,127,6); pan(&d,8,-127,127,6); end(&d); end(&d); parse(&s,&d);
    uint8_t report[] = {1,0}, feature = 1;
    assert(!tm_hid_pan_ready(&s) && input(&s,report,2,0) == 0);
    assert(tm_hid_pan_feature(&s,0,&feature,1) == 1 && tm_hid_pan_ready(&s));
    for (unsigned i = 0; i < 7; ++i) assert(input(&s,report,2,1) == 0);
    assert(input(&s,report,2,1) == 1);
    report[0] = 0xff;
    for (unsigned i = 0; i < 7; ++i) assert(input(&s,report,2,1) == 0);
    assert(input(&s,report,2,1) == -1);
    report[0] = 4; assert(input(&s,report,2,1) == 0);
    report[0] = 0xfc; assert(input(&s,report,2,1) == 0); /* Fractional reversal. */
    assert(s.field[0].fraction == 0);
    report[0] = 7; report[1] = 0xf9;
    assert(input(&s,report,2,1) == 0); /* Fractions must not cancel across axes. */
    report[0] = 1; report[1] = 0; assert(input(&s,report,2,1) == 1);
    assert(s.field[1].fraction == -105);
    tm_hid_pan_clear(&s); assert(!s.field[0].fraction && !s.field[1].fraction);
    report[0] = 7; assert(input(&s,report,2,1) == 0);
    assert(input(&s,report,1,-1) == 0); /* No partial update on truncated multi-axis report. */
    report[0] = 1; assert(input(&s,report,2,1) == 0);
    feature = 3; tm_hid_pan before = s;
    assert(tm_hid_pan_feature(&s,0,&feature,1) == -1 && !memcmp(&s,&before,sizeof s));
    feature = 0; assert(tm_hid_pan_feature(&s,0,&feature,1) == 1);
    assert(s.field[0].multiplier == 1 && !s.field[0].fraction);
    assert(input(&s,report,2,1) == 1);
    /* Signed multiplier values must remain signed. Negative physical scale
     * reverses a report, matching the kernel's resolution convention. */
    d = (descriptor){0}; multiplier(&d,0,-1,0,-8,(uint32_t)-1,8); pan(&d,8,-127,127,6);
    parse(&s,&d); feature = 0xff; assert(tm_hid_pan_feature(&s,0,&feature,1) == 1);
    report[0] = 8; assert(input(&s,report,1,1) == -1);
    /* Invalid effective scales (zero / outside +/-255) fall back to one. */
    for (unsigned value = 0; value < 2; ++value) {
        d = (descriptor){0}; multiplier(&d,0,0,1,0,256,8); pan(&d,8,-127,127,6);
        parse(&s,&d); feature = value;
        assert(tm_hid_pan_feature(&s,0,&feature,1) == 1 && s.field[0].multiplier == 1);
    }
}
static void scoped_multipliers(void)
{
    tm_hid_pan s; descriptor d = {0};
    begin(&d,1); begin(&d,2); multiplier(&d,2,0,1,1,4,8);
    global(&d,8,1,5); pan(&d,8,-127,127,6); end(&d);
    begin(&d,2); multiplier(&d,3,0,1,1,8,8);
    global(&d,8,1,6); pan(&d,8,-127,127,6); end(&d); end(&d);
    parse(&s,&d); uint8_t feature = 1;
    assert(tm_hid_pan_feature(&s,3,&feature,1) == 1 && !tm_hid_pan_ready(&s));
    assert(tm_hid_pan_feature(&s,2,&feature,1) == 1 && tm_hid_pan_ready(&s));
    assert(s.field[0].multiplier == 4 && s.field[1].multiplier == 8);
    uint8_t report[] = {5,4}; assert(input(&s,report,2,1) == 1);
    report[0] = 6; assert(input(&s,report,2,1) == 0);
    assert(input(&s,report,2,1) == 1);
    /* Registered feature-report order, rather than GET completion order,
     * decides which of multiple applicable multiplier fields wins. */
    d = (descriptor){0}; begin(&d,1);
    multiplier(&d,9,0,1,1,2,8); multiplier(&d,4,0,1,1,4,8);
    multiplier(&d,9,0,1,1,8,8); global(&d,8,1,7); pan(&d,8,-127,127,6); end(&d);
    parse(&s,&d); uint8_t ninth[] = {1,1};
    assert(tm_hid_pan_feature(&s,4,&feature,1) == 1);
    assert(tm_hid_pan_feature(&s,9,ninth,2) == 1);
    assert(s.field[0].multiplier == 4);
    /* Push/pop restores the full globals, including report ID and range. */
    d = (descriptor){0}; pan(&d,8,-127,127,6); global(&d,10,0,0);
    multiplier(&d,2,0,1,1,2,8); global(&d,11,0,0); pan(&d,8,-127,127,6); parse(&s,&d);
    assert(!s.numbered && s.field[1].report == 0 && s.field[1].offset == 8);
    assert(tm_hid_pan_feature(&s,2,&feature,1) == 1);
    uint8_t both[] = {2,0xfe}; assert(input(&s,both,2,1) == 0);
    /* Multi-value feature fields use their first value, including when the
     * multiplier is the second declared usage. A short GET is atomic. */
    d = (descriptor){0}; global(&d,0,1,1); local(&d,0,1,0x47); local(&d,0,1,0x48);
    range(&d,0,1); global(&d,3,1,1); global(&d,4,1,8);
    global(&d,7,1,8); global(&d,9,1,2); main_item(&d,11,1,2);
    pan(&d,8,-127,127,6); parse(&s,&d); uint8_t values[] = {1,0};
    tm_hid_pan before = s;
    assert(tm_hid_pan_feature(&s,0,values,1) == -1 && !memcmp(&s,&before,sizeof s));
    assert(tm_hid_pan_feature(&s,0,values,2) == 1 && s.field[0].multiplier == 8);
    /* Only the first delimited usage branch is used, matching Linux. */
    d = (descriptor){0}; global(&d,0,1,12); local(&d,10,1,1); local(&d,0,2,0x238);
    local(&d,10,1,0); local(&d,10,1,1); local(&d,0,2,0x239); local(&d,10,1,0);
    range(&d,-127,127); global(&d,7,1,8); global(&d,9,1,1); main_item(&d,8,1,6);
    parse(&s,&d); assert(s.count == 1);
}
static void null_and_malformed(void)
{
    tm_hid_pan s; descriptor d = {0}; pan(&d,8,-10,10,6 | 64); parse(&s,&d);
    uint8_t null = 11; assert(input(&s,&null,1,0) == 0);
    null = 0xff; assert(input(&s,&null,1,1) == -1);
    static const uint8_t bad[][6] = {
        {0x0b,1}, {0xfe,4,0,0,0}, {0xb4}, {0xc0}, {0xa1,1},
        {0x85,0}, {0x19,2,0x29,1}, {0x29,1}, {0x75,0,0x95,1,0x81,6}
    };
    const unsigned length[] = {2,5,1,1,2,2,4,2,6};
    for (unsigned i = 0; i < sizeof length / sizeof *length; ++i) {
        memset(&s,0xff,sizeof s); assert(tm_hid_pan_parse(&s,bad[i],length[i]) == -1);
        tm_hid_pan zero = {0}; assert(!memcmp(&s,&zero,sizeof s));
    }
    d = (descriptor){0}; pan(&d,33,-127,127,6); assert(tm_hid_pan_parse(&s,d.data,d.size) == -1);
    d = (descriptor){0}; pan(&d,8,10,9,6); assert(tm_hid_pan_parse(&s,d.data,d.size) == -1);
    d = (descriptor){0}; global(&d,7,4,UINT32_MAX); global(&d,9,4,UINT32_MAX);
    main_item(&d,8,1,3); assert(tm_hid_pan_parse(&s,d.data,d.size) == -1);
    d = (descriptor){0}; for (unsigned i = 0; i < 17; ++i) global(&d,10,0,0);
    assert(tm_hid_pan_parse(&s,d.data,d.size) == -1);
    d = (descriptor){0}; for (unsigned i = 0; i < TM_HID_COLLECTIONS+1; ++i) begin(&d,1);
    assert(tm_hid_pan_parse(&s,d.data,d.size) == -1);
    for (size_t i = 0; i < sizeof mouse; ++i) assert(tm_hid_pan_parse(&s,mouse,i) <= 0);
    /* Bounded deterministic mutation corpus exercises all descriptor byte
     * positions and lengths under ASan/UBSan, then decodes every accepted form. */
    uint32_t random = 0xa813beef; uint8_t report[4096] = {0};
    for (unsigned i = 0; i < 20000; ++i) {
        uint8_t mutated[sizeof mouse]; memcpy(mutated,mouse,sizeof mouse);
        random = random * 1664525 + 1013904223;
        mutated[random % sizeof mouse] ^= (uint8_t)(random >> 24);
        int result = tm_hid_pan_parse(&s,mutated,sizeof mutated);
        if (result < 0) continue;
        for (unsigned id = 0; id < 256; ++id) {
            if (!s.input_bits[id]) continue;
            report[0] = (uint8_t)id;
            int64_t delta; assert(tm_hid_pan_input(&s,report,sizeof report,&delta) >= 0);
        }
    }
}
int main(void)
{
    plain_reports(); numbered_and_wide(); scaling_and_loss();
    scoped_multipliers(); null_and_malformed();
    puts("HID pan descriptor, scaling, scope, signed reports, loss and mutation checks passed");
    return 0;
}
