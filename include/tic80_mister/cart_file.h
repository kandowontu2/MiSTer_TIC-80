#ifndef TIC80_MISTER_CART_FILE_H
#define TIC80_MISTER_CART_FILE_H
#include <stddef.h>
#include <stdint.h>
/* Accept native .tic envelopes or bounded PNG containers. PNG payload/image
 * decoding is separate so live players perform it inside their supervised VM. */
int tm_cart_file_validate(const uint8_t *bytes, size_t size);
/* On success returns an owned native .tic buffer; free it after tic80_load.
 * Both input and extracted native payload are limited to 4 MiB. */
int tm_cart_file_decode(const uint8_t *bytes, size_t size, uint8_t **native, size_t *native_size);
#endif
