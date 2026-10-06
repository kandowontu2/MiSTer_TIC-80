#ifndef TIC80_MISTER_VIDEO_H
#define TIC80_MISTER_VIDEO_H
#include <stddef.h>
#include <stdint.h>

/* Preserve TIC-80's border callbacks: full output is 256x144. */
#include "tic80_mister/memory_map.h"

/* Source must use TIC80_PIXEL_COLOR_RGBA8888 (R,G,B,A bytes).
 * Destination contains packed R,G,B,A bytes; the FPGA ignores alpha and
 * preserves the full eight bits of each color channel.
 * Buffers must not overlap. Returns 0 on success, -1 for invalid sizes/pointers.
 * This converts a frame; it does not map or publish shared DDR memory. */
int tm_video_convert(uint8_t *dst, size_t dst_size,
                     const uint8_t *rgba, size_t rgba_size, size_t pitch);
#endif
