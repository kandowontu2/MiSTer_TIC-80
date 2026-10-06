#include "tic80_mister/memory_equal.h"
#include <stdint.h>
#include <string.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
bool tm_memory_equal(const void *left, const void *right, size_t size)
{
#ifdef __ARM_NEON
    const uint8_t *a=left, *b=right;
    uint8x16_t difference=vdupq_n_u8(0);
    /* Keep both complete ranges in the comparison. Static code/binary data
     * receive the same coverage as writable asset banks. */
    while(size>=64) {
        if(size>=256) { __builtin_prefetch(a+128); __builtin_prefetch(b+128); }
        for(unsigned i=0;i<64;i+=16)
            difference=vorrq_u8(difference,veorq_u8(vld1q_u8(a+i),vld1q_u8(b+i)));
        a+=64; b+=64; size-=64;
    }
    uint64x2_t lanes=vreinterpretq_u64_u8(difference);
    if(vgetq_lane_u64(lanes,0) || vgetq_lane_u64(lanes,1)) return false;
    return !size || memcmp(a,b,size)==0;
#else
    return !size || memcmp(left,right,size)==0;
#endif
}
