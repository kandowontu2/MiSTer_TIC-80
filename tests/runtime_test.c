#include "tic80.h"
#include "tic.h"
#include "cart.h"
#include "api.h"
#include "tic80_mister/video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int errors;
static u64 ticks;
static void error(const char *message) { fprintf(stderr, "%s\n", message); ++errors; }
static u64 counter(void *data) { (void)data; return ticks; }
static u64 frequency(void *data) { (void)data; return 60; }

int main(void)
{
    static tic_cartridge cart;
    /* Exercise the real Lua VM, four-player input, raster palette changes,
     * persistence API and the sound synthesizer, not a mocked player. */
    strcpy(cart.code.data,
        "-- script: lua\n"
        "n=0\n"
        "function TIC()\n"
        " n=n+1; cls(0); pix(0,0,1); pix(239,135,1)\n"
        " poke(0x3ff8,1)\n"
        " vbank(1); cls(0); poke(0x3fc6,0); poke(0x3fc7,255); poke(0x3fc8,0); pix(10,10,2); vbank(0)\n"
        " for i=0,31 do if btn(i) then pix(i,1,1) end end\n"
        " pmem(0,n)\n"
        " if n==1 then sfx(0,'C-4',-1,0,15,0) end\n"
        "end\n"
        "function BDR(row)\n"
        " poke(0x3fc3,255); poke(0x3fc4,row); poke(0x3fc5,0)\n"
        "end\n");
    cart.bank0.palette.vbank0.colors[1] = (tic_rgb){255, 0, 0};
    memset(cart.bank0.sfx.waveforms.items[0].data, 0xF0, sizeof cart.bank0.sfx.waveforms.items[0].data);
    size_t capacity = sizeof cart * 2;
    u8 *bytes = malloc(capacity);
    CHECK(bytes);
    s32 size = tic_cart_save(&cart, bytes);
    CHECK(size > 0 && (size_t)size < capacity);
    FILE *file = fopen("smoke.tic", "wb");
    CHECK(file);
    CHECK(fwrite(bytes, 1, size, file) == (size_t)size);
    CHECK(fclose(file) == 0);
    tic80 *tic = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    tic->callback.error = error;
    tic80_load(tic, bytes, size);
    free(bytes);
    tic80_input input = {0};
    input.gamepads.data = 0x80402010; /* Different button on each of four players. */
    int nonzero_audio = 0;
    for (ticks = 0; ticks < 120; ++ticks) {
        tic80_tick(tic, input, counter, frequency);
        tic80_sound(tic);
        CHECK(!errors);
        CHECK(tic->samples.count == 1600);
        for (int i = 0; i < tic->samples.count; ++i)
            if (tic->samples.buffer[i]) nonzero_audio = 1;
    }
    CHECK(tic_api_pmem((tic_mem *)tic, 0, 0, false) == 120);
    const u8 *rgba = (const u8 *)tic->screen;
    const size_t corner = (TIC80_MARGIN_TOP * TIC80_FULLWIDTH + TIC80_MARGIN_LEFT) * 4;
    CHECK(rgba[corner] == 255 && rgba[corner + 2] == 0);
    CHECK(rgba[corner + 1] == TIC80_MARGIN_TOP);
    const size_t bottom_corner = ((TIC80_MARGIN_TOP + 135) * TIC80_FULLWIDTH + TIC80_MARGIN_LEFT + 239) * 4;
    CHECK(rgba[bottom_corner] == 255 && rgba[bottom_corner + 1] == TIC80_MARGIN_TOP + 135);
    const size_t overlay = ((TIC80_MARGIN_TOP + 10) * TIC80_FULLWIDTH + TIC80_MARGIN_LEFT + 10) * 4;
    CHECK(rgba[overlay] == 0 && rgba[overlay + 1] == 255 && rgba[overlay + 2] == 0);
    CHECK(rgba[0] == 255 && rgba[1] == 0);
    const size_t bottom_border = (TIC80_FULLHEIGHT - 1) * TIC80_FULLWIDTH * 4;
    CHECK(rgba[bottom_border] == 255 && rgba[bottom_border + 1] == TIC80_FULLHEIGHT - 1);
    for (int i = 0; i < 32; ++i) {
        size_t pixel = ((TIC80_MARGIN_TOP + 1) * TIC80_FULLWIDTH + TIC80_MARGIN_LEFT + i) * 4;
        CHECK((rgba[pixel] == 255) == !!(input.gamepads.data & (1u << i)));
    }
    CHECK(nonzero_audio);
    static u8 transported[TM_FRAME_BYTES];
    CHECK(tm_video_convert(transported, sizeof transported, rgba,
        TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4, TIC80_FULLWIDTH*4) == 0);
    CHECK(!memcmp(transported, rgba, sizeof transported)); // every raster palette level survives
    tic80_delete(tic);
    puts("Lua cartridge, four-player input, pmem, video and audio passed");
    return 0;
}
