#ifndef TIC80_MISTER_MEMORY_EQUAL_H
#define TIC80_MISTER_MEMORY_EQUAL_H
#include <stdbool.h>
#include <stddef.h>
/* Equality only; reads exactly the supplied byte ranges, including unaligned
 * tails. Unlike memcmp, no ordering result is needed by a checkpoint scan. */
bool tm_memory_equal(const void *left, const void *right, size_t size);
#endif
