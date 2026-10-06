# Keep the pinned interpreter unchanged; replace only its allocation entry point.
set(wasm_env_original "${THIRDPARTY_DIR}/wasm3/source/m3_env.c")
file(SHA256 "${wasm_env_original}" wasm_env_sha)
if(NOT wasm_env_sha STREQUAL "abab7da8aabf477bbab2957b92da6c41e1c4cfdcaaa95df345d52516b8ae2e3a")
    message(FATAL_ERROR "Pinned WASM environment allocation source changed")
endif()
file(READ "${wasm_env_original}" wasm_env_code)
foreach(anchor IN ITEMS
    "M3Result  ResizeMemory  (IM3Runtime io_runtime, u32 i_numPages)"
    "result = ResizeMemory (io_runtime, i_module->memoryInfo.initPages);"
    "    if (not i_module->memoryImported)\n    {\n        u32 maxPages = i_module->memoryInfo.maxPages;")
    string(FIND "${wasm_env_code}" "${anchor}" anchor_at)
    if(anchor_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM memory entry point missing: ${anchor}")
    endif()
endforeach()
string(REPLACE "M3Result  ResizeMemory  (IM3Runtime io_runtime, u32 i_numPages)"
    "static M3Result tm_upstream_wasm_resize_memory(IM3Runtime io_runtime, u32 i_numPages)"
    wasm_env_code "${wasm_env_code}")
string(REPLACE "result = ResizeMemory (io_runtime, i_module->memoryInfo.initPages);"
    "result = tm_wasm_initialize_memory(io_runtime, i_module->memoryImported\n            ? M3_MIN(TIC_WASM_PAGE_COUNT, io_runtime->memory.maxPages)\n            : i_module->memoryInfo.initPages);"
    wasm_env_code "${wasm_env_code}")
# Imported memory uses the preallocated backing too. Its logical size and
# maximum must fit the declared import, including the standard two-page demo.
string(REPLACE "    if (not i_module->memoryImported)\n    {\n        u32 maxPages = i_module->memoryInfo.maxPages;"
    "    {\n        u32 maxPages = i_module->memoryInfo.maxPages;"
    wasm_env_code "${wasm_env_code}")
set(wasm_env_generated "${CMAKE_BINARY_DIR}/runtime_adapters/wasm_env.inc")
tm_write_generated("${wasm_env_generated}" "${wasm_env_code}")
get_target_property(wasm_env_sources wasm SOURCES)
list(REMOVE_ITEM wasm_env_sources "${wasm_env_original}")
set_property(TARGET wasm PROPERTY SOURCES "${wasm_env_sources}")
target_sources(wasm PRIVATE "${TM_INTEGRATION_SOURCE}/src/runtime_wasm_env.c")
target_compile_definitions(wasm PRIVATE TM_UPSTREAM_WASM_ENV="${wasm_env_generated}")
