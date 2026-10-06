# Preserve the pinned parser while repairing memory validation and error handling.
set(wasm_parse_original "${THIRDPARTY_DIR}/wasm3/source/m3_parse.c")
file(SHA256 "${wasm_parse_original}" wasm_parse_sha)
if(NOT wasm_parse_sha STREQUAL "4caa0aab65898f68a0f30022570984076f476a98dcc7cc8208dde2dd0549c70f")
    message(FATAL_ERROR "Pinned WASM memory parser changed")
endif()
file(READ "${wasm_parse_original}" wasm_parse_code)
foreach(anchor IN ITEMS
    "M3Result  ParseType_Memory  (M3MemoryInfo * o_memory, bytes_t * io_bytes, cbytes_t i_end)"
    "    ParseType_Memory (& io_module->memoryInfo, & i_bytes, i_end);")
    string(FIND "${wasm_parse_code}" "${anchor}" anchor_at)
    if(anchor_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM memory parser anchor missing: ${anchor}")
    endif()
endforeach()
string(REPLACE "M3Result  ParseType_Memory  (M3MemoryInfo * o_memory, bytes_t * io_bytes, cbytes_t i_end)"
    "static M3Result tm_upstream_wasm_parse_memory(M3MemoryInfo * o_memory, bytes_t * io_bytes, cbytes_t i_end)"
    wasm_parse_code "${wasm_parse_code}")
# The original memory-section reader ignores the memory-type error result.
string(REPLACE "    ParseType_Memory (& io_module->memoryInfo, & i_bytes, i_end);"
    "_   (ParseType_Memory (& io_module->memoryInfo, & i_bytes, i_end));"
    wasm_parse_code "${wasm_parse_code}")
set(wasm_parse_generated "${CMAKE_BINARY_DIR}/runtime_adapters/wasm_parse.inc")
tm_write_generated("${wasm_parse_generated}" "${wasm_parse_code}")
get_target_property(wasm_parse_sources wasm SOURCES)
list(REMOVE_ITEM wasm_parse_sources "${wasm_parse_original}")
set_property(TARGET wasm PROPERTY SOURCES "${wasm_parse_sources}")
target_sources(wasm PRIVATE "${TM_INTEGRATION_SOURCE}/src/runtime_wasm_parse.c")
target_compile_definitions(wasm PRIVATE TM_UPSTREAM_WASM_PARSE="${wasm_parse_generated}")
