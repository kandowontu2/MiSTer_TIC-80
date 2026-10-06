#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/pmem.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static u64 tick;
static u64 counter(void *data) { (void)data; return tick; }
static u64 frequency(void *data) { (void)data; return 60; }
static int errors;
static void error(const char *s) { fprintf(stderr, "%s\n", s); ++errors; }
int main(void)
{
    char dir[] = "/tmp/tic80-pmem-XXXXXX";
    assert(mkdtemp(dir));
    char path[256];
    snprintf(path, sizeof path, "%s/save.pmem", dir);
    static tic_cartridge cart;
    strcpy(cart.code.data, "-- script: lua\nfunction TIC() pmem(0,pmem(0)+1); pmem(255,0xfedcba98) end\n");
    uint8_t *bytes = malloc(sizeof cart * 2);
    assert(bytes);
    s32 size = tic_cart_save(&cart, bytes);
    assert(size > 0);
    for (unsigned run = 0; run < 2; ++run) {
        tic80 *tic = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
        tic->callback.error = error;
        tic80_load(tic, bytes, size);
        tm_pmem save;
        assert(tm_pmem_open(&save, tic, path) == 0);
        assert(((tic_mem *)tic)->ram->persistent.data[0] == run * 10);
        for (tick = 0; tick < 10; ++tick) {
            tic80_input input = {0};
            tic80_tick(tic, input, counter, frequency);
            assert(tm_pmem_schedule(&save, tic) == 0);
        }
        assert(!errors);
        assert(tm_pmem_save(&save, tic) == 0);
        struct stat st;
        assert(stat(path, &st) == 0 && st.st_size == 1036);
        assert(tm_pmem_save(&save, tic) == 0);
        tm_pmem_close(&save);
        tic80_delete(tic);
    }
    tic80 *tic = tic80_create(48000, TIC80_PIXEL_COLOR_RGBA8888);
    tic80_load(tic, bytes, size);
    tm_pmem save;
    assert(tm_pmem_open(&save, tic, path) == 0);
    assert(((tic_mem *)tic)->ram->persistent.data[0] == 20);
    assert(((tic_mem *)tic)->ram->persistent.data[255] == 0xfedcba98);
    tm_pmem_close(&save);
    // A supervisor owns a private acknowledged array, not the worker's tic_mem.
    // Cross both interfaces to verify a single format and full-width values.
    uint32_t snapshot[256]={0};
    assert(tm_pmem_open_values(&save,snapshot,path)==0);
    assert(snapshot[0]==20 && snapshot[255]==0xfedcba98);
    snapshot[0]=21; snapshot[255]=0x89abcdef;
    assert(tm_pmem_schedule_values(&save,snapshot)==0);
    // Mutating the source after scheduling must not change the queued copy.
    snapshot[0]=999; snapshot[255]=0;
    tm_pmem_close(&save); // drain the exact queued snapshot
    assert(tm_pmem_open(&save,tic,path)==0);
    assert(((tic_mem *)tic)->ram->persistent.data[0]==21);
    assert(((tic_mem *)tic)->ram->persistent.data[255]==0x89abcdef);
    tm_pmem_close(&save);
    assert(tm_pmem_open_values(&save,snapshot,path)==0);
    snapshot[0]=22;
    assert(tm_pmem_schedule_values(&save,snapshot)==0);
    snapshot[0]=23; snapshot[255]=0xffffffff;
    assert(tm_pmem_save_values(&save,snapshot)==0); // newest final state wins
    tm_pmem_close(&save);
    assert(tm_pmem_open(&save,tic,path)==0);
    assert(((tic_mem *)tic)->ram->persistent.data[0]==23);
    assert(((tic_mem *)tic)->ram->persistent.data[255]==0xffffffff);
    tm_pmem_close(&save);
    FILE *f = fopen(path, "r+b");
    assert(f && fseek(f, 16, SEEK_SET) == 0);
    assert(fputc(123, f) != EOF && fclose(f) == 0);
    assert(tm_pmem_open(&save, tic, path) != 0);
    assert(!save.path); // corrupt save cannot be overwritten through this context
    for(unsigned i=0;i<256;++i) snapshot[i]=0xa5a5a5a5;
    uint32_t untouched[256]; memcpy(untouched,snapshot,sizeof snapshot);
    assert(tm_pmem_open_values(&save,snapshot,path)!=0);
    assert(!save.path && !memcmp(snapshot,untouched,sizeof snapshot));
    assert(tm_pmem_schedule_values(&save,snapshot)!=0);
    assert(tm_pmem_save_values(&save,snapshot)!=0);
    tic80_delete(tic);
    free(bytes);
    assert(unlink(path) == 0 && rmdir(dir) == 0);
    puts("pmem survives runtime recreation, shares runtime/snapshot format, copies queued values, drains newest final state and rejects corruption without mutation");
    return 0;
}
