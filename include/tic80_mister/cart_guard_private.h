#ifndef TIC80_MISTER_CART_GUARD_PRIVATE_H
#define TIC80_MISTER_CART_GUARD_PRIVATE_H
#include <stddef.h>
#include <stdbool.h>
typedef struct tm_cart_guard tm_cart_guard;
/* One owner thread publishes a heap cartridge. Only full interior OS pages
 * are protected; boundary pages are always compared by the caller. */
tm_cart_guard *tm_cart_guard_open(void *bytes,size_t size);
int tm_cart_guard_begin(tm_cart_guard *guard);
bool tm_cart_guard_maybe_changed(const tm_cart_guard *guard,size_t offset,size_t size);
void tm_cart_guard_close(tm_cart_guard *guard);
#endif
