#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static tic80 *players[2];
static u64 clocks[2];
static int errors;
static void error(const char *message) { fprintf(stderr, "%s\n", message); ++errors; }
static u64 counter(void *data) { return clocks[data == players[1] ? 1 : 0]; }
static u64 frequency(void *data) { (void)data; return 1000; }
static u32 value(tic80 *tic, int slot) { return tic_api_pmem((tic_mem *)tic, slot, 0, false); }
static void tick(int index) { tic80_tick(players[index], (tic80_input){0}, counter, frequency); }

int main(void)
{
    static tic_cartridge cart;
    strcpy(cart.code.data,
        "-- script: lua\n"
        "function BOOT() pmem(0,time()) end\n"
        "function TIC() pmem(1,time()) end\n"
        "function BDR(row) if row==143 then pmem(2,time()) end end\n");
    u8 *bytes = malloc(sizeof cart * 2);
    CHECK(bytes);
    s32 size = tic_cart_save(&cart, bytes);
    for (int i = 0; i < 2; ++i) {
        players[i] = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
        CHECK(players[i]);
        players[i]->callback.error = error;
        tic80_load(players[i], bytes, size);
    }
    clocks[0] = 9000000000000ULL;
    tick(0);
    CHECK(!errors && value(players[0], 0) == 0 && value(players[0], 1) == 0);
    clocks[0] += 17;
    tick(0);
    CHECK(!errors && value(players[0], 1) == 17 && value(players[0], 2) == 17);
    CHECK(tic_api_time((tic_mem *)players[0]) == 17);
    clocks[1] = 120000;
    tick(1);
    CHECK(value(players[1], 0) == 0 && value(players[1], 1) == 0);
    clocks[1] += 29;
    tick(1);
    CHECK(value(players[1], 1) == 29);
    clocks[0] += 23;
    tick(0);
    CHECK(value(players[0], 1) == 40);

    tic_core_pause((tic_mem *)players[0]);
    clocks[0] += 2000;
    tic_core_resume((tic_mem *)players[0]);
    clocks[0] += 13;
    tick(0);
    CHECK(value(players[0], 1) == 53);
    tic80_load(players[0], bytes, size);
    tick(0);
    CHECK(value(players[0], 0) == 0 && value(players[0], 1) == 0);
    clocks[0] += 31;
    tick(0);
    CHECK(value(players[0], 1) == 31);
    tic80_delete(players[0]);
    clocks[1] += 7;
    tick(1);
    CHECK(!errors && value(players[1], 1) == 36);
    tic80_delete(players[1]);
    free(bytes);
    puts("Clock epoch, independent VMs, raster callbacks, pause/resume and reload passed");
    return 0;
}
