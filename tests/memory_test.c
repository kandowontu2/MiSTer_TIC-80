#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static u64 counter(void *data) { (void)data; return 1; }
static u64 frequency(void *data) { (void)data; return 60; }
static void error(const char *message) { fprintf(stderr, "%s\n", message); exit(1); }

static size_t allocated(void)
{
#if __GLIBC_PREREQ(2, 33)
    struct mallinfo2 info = mallinfo2();
#else
    struct mallinfo info = mallinfo();
#endif
    return (size_t)info.uordblks + (size_t)info.hblkhd;
}

static void cycle(const u8 *bytes, s32 size)
{
    tic_mem *core = tic_core_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    CHECK(core);
    tic_core_close(core);
    tic80 *unused = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    CHECK(unused);
    tic80_delete(unused);
    tic80 *player = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    CHECK(player);
    player->callback.error = error;
    tic80_load(player, (void *)bytes, size);
    tic80_tick(player, (tic80_input){0}, counter, frequency);
    tic80_sound(player);
    tic80_delete(player);
}

int main(void)
{
    tic_cartridge *cart = calloc(1, sizeof *cart);
    CHECK(cart);
    strcpy(cart->code.data, "-- script: lua\nfunction TIC() cls(2) end\n");
    u8 *bytes = malloc(sizeof *cart * 2);
    CHECK(bytes);
    s32 size = tic_cart_save(cart, bytes);
    CHECK(size > 0);
    /* Warm allocator bins and Lua's process-wide setup before measuring live
     * allocations. Include mapped blocks so large core allocations count too. */
    for (unsigned i = 0; i < 16; ++i) cycle(bytes, size);
    size_t baseline = allocated();
    for (unsigned i = 0; i < 64; ++i) cycle(bytes, size);
    size_t final = allocated();
    printf("192 core/unused/Lua player closures: live heap %zu -> %zu bytes\n", baseline, final);
    CHECK(final <= baseline + 64 * 1024);
    free(bytes);
    free(cart);
    return 0;
}
