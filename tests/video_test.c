#include "tic80_mister/video.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
    enum { PITCH = TM_WIDTH * 4 + 16 };
    static uint8_t rgba[PITCH * TM_HEIGHT];
    static uint8_t dst[TM_FRAME_BYTES + 1];
    memset(rgba, 0xEE, sizeof rgba); // row padding must never enter the frame
    for (unsigned y=0; y<TM_HEIGHT; ++y) for (unsigned x=0; x<TM_WIDTH; ++x) {
        uint8_t *p=rgba+y*PITCH+x*4;
        p[0]=(uint8_t)x; p[1]=(uint8_t)(x+y); p[2]=(uint8_t)(255-x); p[3]=(uint8_t)(y^x);
    }
    memset(dst, 0xA5, sizeof dst);
    rgba[0] = 255; /* red */
    rgba[5] = 255; /* green */
    rgba[10] = 255; /* blue */
    rgba[PITCH] = 255; rgba[PITCH + 1] = 255; rgba[PITCH + 2] = 255;
    const size_t last = (TM_HEIGHT - 1) * PITCH + (TM_WIDTH - 1) * 4;
    rgba[last + 2] = 255;
    CHECK(tm_video_convert(dst, TM_FRAME_BYTES, rgba, sizeof rgba, PITCH) == 0);
    for (unsigned y=0; y<TM_HEIGHT; ++y)
        CHECK(!memcmp(dst+y*TM_WIDTH*4, rgba+y*PITCH, TM_WIDTH*4));
    CHECK(dst[TM_FRAME_BYTES] == 0xA5);
    CHECK(tm_video_convert(dst, TM_FRAME_BYTES - 1, rgba, sizeof rgba, PITCH) == -1);
    CHECK(tm_video_convert(dst, sizeof dst, rgba, last + 3, PITCH) == -1);
    CHECK(tm_video_convert(dst, sizeof dst, rgba, sizeof rgba, SIZE_MAX) == -1);
    CHECK(tm_video_convert(dst, sizeof dst, rgba, sizeof rgba, 1) == -1);
    CHECK(tm_video_convert(NULL, sizeof dst, rgba, sizeof rgba, PITCH) == -1);
    puts("All 256 color levels and alpha, complete frame, padded stride and bounds preserved exactly");
    return 0;
}
