#include "tic80_mister/video.h"
#include <string.h>

int tm_video_convert(uint8_t *dst, size_t dst_size,
                     const uint8_t *rgba, size_t rgba_size, size_t pitch)
{
    const size_t row_bytes = TM_WIDTH * 4;
    if (!dst || !rgba || dst_size < TM_FRAME_BYTES || pitch < row_bytes)
        return -1;
    if (pitch > (SIZE_MAX - row_bytes) / (TM_HEIGHT - 1) ||
        rgba_size < pitch * (TM_HEIGHT - 1) + row_bytes)
        return -1;
    for (size_t y = 0; y < TM_HEIGHT; ++y) {
        const uint8_t *row = rgba + y * pitch;
        memcpy(dst + y * row_bytes, row, row_bytes);
    }
    return 0;
}
