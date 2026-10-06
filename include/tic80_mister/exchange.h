#ifndef TIC80_MISTER_EXCHANGE_H
#define TIC80_MISTER_EXCHANGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sequence;
    uint32_t publication;
    unsigned front, acquired_buffer;
    int pending, acquired, has_front;
} tm_exchange;

/* Call only after FPGA session acknowledgment, with publication cleared and
 * no displayed frame. Session reset/identity/heartbeat checking is performed
 * by the hardware backend before this state is initialized. */
void tm_exchange_init(tm_exchange *exchange);
/* Returns 1 with a writable buffer, 0 while waiting for adoption, -1 on a
 * protocol/state error. presented bit1=valid; bit0=buffer; upper30=sequence. */
int tm_exchange_acquire(tm_exchange *exchange, uint32_t presented, unsigned *buffer);
/* After all pixel writes and a release barrier, publish the returned word.
 * FPGA may adopt it only at vblank. Returns -1 without an acquired buffer. */
int tm_exchange_publish(tm_exchange *exchange, uint32_t *publication);
/* Ring counters count stereo sample frames, not bytes or individual samples.
 * Unsigned subtraction handles wrap; returns -1 for impossible occupancy. */
int tm_audio_available(uint32_t write, uint32_t read, uint32_t *available);
#ifdef __cplusplus
}
#endif
#endif
