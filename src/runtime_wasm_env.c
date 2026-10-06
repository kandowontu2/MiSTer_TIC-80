/* TIC-80 shares its RAM with WASM linear memory. Reserve the machine's full
 * 256 KiB once; changing the module's logical page count must not move it. */
#include "tic.h"
#include "m3_env.h"
static M3Result tm_wasm_initialize_memory(IM3Runtime, u32);
#include TM_UPSTREAM_WASM_ENV

static M3Result tm_wasm_resize_memory(IM3Runtime runtime, u32 pages, bool initialize)
{
    M3Memory *memory = &runtime->memory;
    const u32 minimum = (TIC_RAM_SIZE + d_m3MemPageSize - 1) / d_m3MemPageSize;
    const u32 maximum = memory->maxPages < TIC_WASM_PAGE_COUNT
        ? memory->maxPages : TIC_WASM_PAGE_COUNT;
    const size_t capacity = (size_t)TIC_WASM_PAGE_COUNT * d_m3MemPageSize;
    if (pages < minimum || pages > maximum
        || (!initialize && pages < memory->numPages)
        || (runtime->memoryLimit && runtime->memoryLimit < capacity))
        return m3Err_wasmMemoryOverflow;

    if (!memory->mallocated) {
        memory->maxPages = TIC_WASM_PAGE_COUNT;
        M3Result problem = tm_upstream_wasm_resize_memory(runtime, TIC_WASM_PAGE_COUNT);
        memory->maxPages = maximum;
        if (problem) return problem;
    }
    size_t bytes = (size_t)pages * d_m3MemPageSize;
    if (bytes > memory->mallocated->length)
        memset((u8 *)m3MemData(memory->mallocated) + memory->mallocated->length,
            0, bytes - memory->mallocated->length);
    memory->numPages = pages;
    memory->maxPages = maximum;
    memory->mallocated->length = bytes;
    return m3Err_none;
}

M3Result ResizeMemory(IM3Runtime runtime, u32 pages)
{
    return tm_wasm_resize_memory(runtime, pages, false);
}

static M3Result tm_wasm_initialize_memory(IM3Runtime runtime, u32 pages)
{
    return tm_wasm_resize_memory(runtime, pages, true);
}
