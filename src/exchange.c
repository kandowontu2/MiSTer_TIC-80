#include "tic80_mister/exchange.h"
#include "tic80_mister/memory_map.h"
#include <string.h>

void tm_exchange_init(tm_exchange *exchange)
{
    memset(exchange, 0, sizeof *exchange);
}

int tm_exchange_acquire(tm_exchange *exchange, uint32_t presented, unsigned *buffer)
{
    if (!exchange || !buffer || exchange->acquired) return -1;
    if (exchange->pending) {
        if (presented != exchange->publication) {
            /* Seeing the previous front buffer while publication is pending
             * is normal; any other acknowledgment is a reset/stale session. */
            uint32_t previous = ((exchange->sequence - 1u) & 0x3FFFFFFFu) << 2;
            previous |= 2u | exchange->front;
            if (exchange->has_front ? presented != previous : presented != 0) return -1;
            return 0;
        }
        exchange->front = exchange->publication & 1u;
        exchange->has_front = 1;
        exchange->pending = 0;
    } else if (exchange->has_front && presented != exchange->publication) return -1;
    else if (!exchange->has_front && presented != 0) return -1;
    exchange->acquired_buffer = exchange->has_front ? (exchange->front ^ 1u) : 0u;
    exchange->acquired = 1;
    *buffer = exchange->acquired_buffer;
    return 1;
}

int tm_exchange_publish(tm_exchange *exchange, uint32_t *publication)
{
    if (!exchange || !publication || !exchange->acquired || exchange->pending) return -1;
    exchange->sequence = (exchange->sequence + 1u) & 0x3FFFFFFFu;
    exchange->publication = (exchange->sequence << 2) | 2u | exchange->acquired_buffer;
    exchange->pending = 1;
    exchange->acquired = 0;
    *publication = exchange->publication;
    return 0;
}

int tm_audio_available(uint32_t write, uint32_t read, uint32_t *available)
{
    uint32_t occupancy = write - read;
    if (!available || occupancy > TM_AUDIO_CAPACITY) return -1;
    *available = TM_AUDIO_CAPACITY - occupancy;
    return 0;
}
