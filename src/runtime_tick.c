/* The pinned embedded API puts tic_tick_data on the stack and resets its
 * epoch every frame. Give each public player a persistent callback context,
 * including its clock epoch, until the VM has finished closing. Direct core
 * APIs use caller-owned tick data; this adapter owns public tic80 players. */
#include "core/core.h"
#define tic80_create tm_upstream_create
#define tic80_tick tm_upstream_tick
#define tic80_delete tm_upstream_delete
#include TM_UPSTREAM_PLAYER
#undef tic80_create
#undef tic80_tick
#undef tic80_delete

tic80 *tic80_create(s32 samplerate, tic80_pixel_color_format format)
{
    tic_tick_data *data = calloc(1, sizeof *data);
    if (!data) return NULL;
    tic_mem *mem = tic_core_create(samplerate, format);
    if (!mem) { free(data); return NULL; }
    ((tic_core *)mem)->data = data;
    return &mem->product;
}

void tic80_tick(tic80 *tic, tic80_input input, CounterCallback counter, FreqCallback freq)
{
    tic_mem *mem = (tic_mem *)tic;
    tic_tick_data *data = ((tic_core *)mem)->data;
    data->error = onError;
    data->trace = onTrace;
    data->exit = onExit;
    data->data = tic;
    data->counter = counter;
    data->freq = freq;
    /* tic_core_tick starts the epoch when initializing a new/reset VM. */
    mem->ram->input = input;
    tic_core_tick_start(mem);
    tic_core_tick(mem, data);
    tic_core_tick_end(mem);
    tic_core_blit(mem);
}

void tic80_delete(tic80 *tic)
{
    tic_tick_data *data = ((tic_core *)tic)->data;
    tm_upstream_delete(tic);
    free(data);
}
