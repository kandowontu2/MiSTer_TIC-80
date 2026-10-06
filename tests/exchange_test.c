#include "tic80_mister/exchange.h"
#include "tic80_mister/memory_map.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
    tm_exchange exchange;
    unsigned buffer = 99;
    uint32_t presented = 0, publication = 0, available;
    tm_exchange_init(&exchange);
    CHECK(tm_exchange_publish(&exchange, &publication) == -1);
    for (unsigned frame = 0; frame < 10000; ++frame) {
        CHECK(tm_exchange_acquire(&exchange, presented, &buffer) == 1);
        if (frame) CHECK(buffer != (presented & 1));
        CHECK(tm_exchange_acquire(&exchange, presented, &buffer) == -1);
        CHECK(tm_exchange_publish(&exchange, &publication) == 0);
        /* FPGA continues displaying the previous frame for an arbitrary delay. */
        for (unsigned delay = 0; delay < frame % 7 + 1; ++delay)
            CHECK(tm_exchange_acquire(&exchange, presented, &buffer) == 0);
        presented = publication;
    }
    /* Exercise 30-bit sequence wrap while retaining front-buffer ownership. */
    tm_exchange_init(&exchange);
    exchange.has_front = 1;
    exchange.front = 1;
    exchange.sequence = 0x3FFFFFFF;
    exchange.publication = 0xFFFFFFFF;
    CHECK(tm_exchange_acquire(&exchange, 0xFFFFFFFF, &buffer) == 1 && buffer == 0);
    CHECK(tm_exchange_publish(&exchange, &publication) == 0 && publication == 2);
    CHECK(tm_exchange_acquire(&exchange, 0xFFFFFFFF, &buffer) == 0);
    CHECK(tm_exchange_acquire(&exchange, 2, &buffer) == 1 && buffer == 1);
    CHECK(tm_audio_available(0, 0, &available) == 0 && available == TM_AUDIO_CAPACITY);
    CHECK(tm_audio_available(4096, 0, &available) == 0 && available == 0);
    CHECK(tm_audio_available(4097, 0, &available) == -1);
    CHECK(tm_audio_available(0, 1, &available) == -1);
    CHECK(tm_audio_available(10, UINT32_MAX - 5, &available) == 0 && available == TM_AUDIO_CAPACITY - 16);
    return 0;
}
