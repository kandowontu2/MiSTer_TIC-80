#ifndef TIC80_MISTER_CART_H
#define TIC80_MISTER_CART_H
#include <stddef.h>
#include <stdint.h>
/* Validate the native .tic chunk envelope before upstream's unchecked loader.
 * PNG containers use the runtime cart_file adapter. Returns 0/-1. */
int tm_cart_validate(const uint8_t *bytes, size_t size);
#endif
