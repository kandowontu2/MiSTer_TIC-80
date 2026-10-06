/* Validate the memory type before the pinned parser discards its limit flag.
 * TIC-80's fixed RAM cannot satisfy an explicitly zero-sized maximum. */
#include "m3_env.h"
#include "m3_compile.h"
M3Result ParseType_Memory(M3MemoryInfo *, bytes_t *, cbytes_t);
#include TM_UPSTREAM_WASM_PARSE

M3Result ParseType_Memory(M3MemoryInfo *memory, bytes_t *bytes, cbytes_t end)
{
    bytes_t cursor = *bytes;
    u8 flag = 0;
    M3Result problem = ReadLEB_u7(&flag, &cursor, end);
    if (problem) return problem;
    if (flag > 1) return "unsupported WASM memory type";
    problem = tm_upstream_wasm_parse_memory(memory, bytes, end);
    if (problem) return problem;
    if (memory->initPages > 65536
        || (flag && (memory->maxPages > 65536
            || memory->maxPages < memory->initPages)))
        return "invalid WASM memory limits";
    if (flag && memory->maxPages == 0)
        return "WASM memory maximum cannot provide TIC-80 RAM";
    return m3Err_none;
}
