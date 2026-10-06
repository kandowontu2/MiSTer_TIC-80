/* The pinned core closes its VM, screen and audio but leaves the original
 * RAM allocation behind. WASM can redirect ram into its module, so release
 * the owned base_ram only after the upstream VM cleanup has finished. */
#include "core/core.h"
#define tic_core_close tm_upstream_core_close
#include TM_UPSTREAM_CORE
#undef tic_core_close

void tic_core_close(tic_mem *memory)
{
    tic_ram *owned_ram = memory->base_ram;
    tm_upstream_core_close(memory);
    free(owned_ram);
}
