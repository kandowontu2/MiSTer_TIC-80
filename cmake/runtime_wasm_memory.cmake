# Apply only to the hash-checked pinned WASM binding staged by runtime_adapters.
function(tm_repair_wasm_pointer_imports input output)
    set(code "${input}")
    set(old_allocation "    ResizeMemory(runtime, TIC_WASM_PAGE_COUNT);")
    string(FIND "${code}" "${old_allocation}" allocation_at)
    if(allocation_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM initial allocation missing")
    endif()
    string(REPLACE "${old_allocation}" [=[    M3Result allocation = ResizeMemory(runtime, TIC_WASM_PAGE_COUNT);
    if (allocation) {
        deinitWasmRuntime(runtime);
        core->data->error(core->data->data, allocation);
        return false;
    }]=] code "${code}")
    set(load_failure [=[    result = m3_LoadModule (runtime, module);
    if (result){
        core->data->error(core->data->data, result);]=])
    string(FIND "${code}" "${load_failure}" load_failure_at)
    if(load_failure_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM module load failure missing")
    endif()
    # Match the TIC initialization/error context, not the separate wasm_load utility.
    string(REPLACE "${load_failure}" [=[    if (module->memoryImported &&
        (module->memoryInfo.initPages > TIC_WASM_PAGE_COUNT ||
         (module->memoryInfo.maxPages && module->memoryInfo.maxPages <
          (TIC_RAM_SIZE + d_m3MemPageSize - 1) / d_m3MemPageSize))) {
        m3_FreeModule(module);
        core->data->error(core->data->data, "WASM imported memory does not fit TIC-80 RAM");
        return false;
    }
    result = m3_LoadModule (runtime, module);
    if (result){
        m3_FreeModule(module);
        core->data->error(core->data->data, result);]=] code "${code}")
    set(first_import "m3ApiRawFunction(wasmtic_line)")
    string(FIND "${code}" "${first_import}" first_import_at)
    if(first_import_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM first import missing")
    endif()
    set(pointer_helpers [=[static bool tm_wasm_pointer_span(IM3Runtime runtime, uint32_t offset, uint32_t length)
{
    uint32_t size = m3_GetMemorySize(runtime);
    return offset <= size && length <= size - offset;
}
static const char* tm_wasm_pointer_string(IM3Runtime runtime, void* memory, uint32_t offset)
{
    uint32_t size = m3_GetMemorySize(runtime);
    if (offset >= size) return NULL;
    const char* text = (const char*)memory + offset;
    return memchr(text, 0, size - offset) ? text : NULL;
}
_Static_assert(sizeof(RemapResult) == 12 && offsetof(RemapResult, flip) == 4
    && offsetof(RemapResult, rotate) == 8, "WASM remap layout changed");

]=])
    string(REPLACE "${first_import}" "${pointer_helpers}${first_import}" code "${code}")
    foreach(declaration IN ITEMS "m3ApiGetArgMem   (const char *, text)" "m3ApiGetArgMem(const char*, text);")
        string(FIND "${code}" "${declaration}" declaration_at)
        if(declaration_at EQUAL -1)
            message(FATAL_ERROR "Pinned WASM text pointer declaration missing")
        endif()
        string(REPLACE "${declaration}" [=[m3ApiGetArg(uint32_t, text_offset);
    const char* text = tm_wasm_pointer_string(runtime, _mem, text_offset);
    if (!text) m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);]=] code "${code}")
    endforeach()
    set(colors_declaration "m3ApiGetArgMem   (u8*, trans_colors)")
    string(REGEX MATCHALL "m3ApiGetArgMem +\\(u8\\*, trans_colors\\)" color_declarations "${code}")
    list(LENGTH color_declarations colors_count)
    if(NOT colors_count EQUAL 4)
        message(FATAL_ERROR "Pinned WASM transparency pointers changed")
    endif()
    string(REPLACE "${colors_declaration}" "m3ApiGetArg(uint32_t, trans_offset);" code "${code}")
    foreach(count IN ITEMS colorCount trans_count)
        set(old_null_guard "    if (trans_colors == NULL) {\n        ${count} = 0;\n    }")
        string(FIND "${code}" "${old_null_guard}" null_guard_at)
        if(null_guard_at EQUAL -1)
            message(FATAL_ERROR "Pinned WASM transparency null guard missing")
        endif()
        set(new_guard "    u8* trans_colors = NULL;\n    if (trans_offset && ${count})\n    {\n        if (!tm_wasm_pointer_span(runtime, trans_offset, (uint8_t)${count}))\n            m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);\n        trans_colors = m3ApiOffsetToPtr(trans_offset);\n    }\n    else ${count} = 0;")
        string(REPLACE "${old_null_guard}" "${new_guard}" code "${code}")
    endforeach()
    set(map_argument "m3ApiGetArg      (i32, map_data_ptr)")
    string(FIND "${code}" "${map_argument}" map_argument_at)
    if(map_argument_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM remap descriptor argument missing")
    endif()
    string(REPLACE "${map_argument}" "m3ApiGetArg(uint32_t, map_data_ptr);" code "${code}")
    set(old_descriptor [=[    if (map_data_ptr > 0) {
        struct MapData* map_data = (struct MapData*)m3ApiOffsetToPtr(map_data_ptr);
        m3_GetTableFunction(&info.fun, runtime->modules, map_data->func_index);
        info.data = map_data->user_data;
        info.wasm_result = (RemapResult*)m3ApiOffsetToPtr(map_data->res_ptr);
    }]=])
    string(FIND "${code}" "${old_descriptor}" descriptor_at)
    if(descriptor_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM remap descriptor binding missing")
    endif()
    set(new_descriptor [=[    if (map_data_ptr > 0) {
        if (!tm_wasm_pointer_span(runtime, map_data_ptr, sizeof(struct MapData)))
            m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
        struct MapData map_data;
        memcpy(&map_data, m3ApiOffsetToPtr(map_data_ptr), sizeof map_data);
        if (!tm_wasm_pointer_span(runtime, map_data.res_ptr, sizeof(RemapResult)))
            m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
        M3Result problem = m3_GetTableFunction(&info.fun, runtime->modules, map_data.func_index);
        if (problem) m3ApiTrap(problem);
        if (!info.fun || m3_GetArgCount(info.fun) != 4 || m3_GetRetCount(info.fun) != 0)
            m3ApiTrap("invalid map callback signature");
        for (uint32_t argument = 0; argument < 4; ++argument)
            if (m3_GetArgType(info.fun, argument) != c_m3Type_i32)
                m3ApiTrap("invalid map callback signature");
        info.data = map_data.user_data;
        info.wasm_result = (uint8_t*)m3ApiOffsetToPtr(map_data.res_ptr);
    }]=])
    string(REPLACE "${old_descriptor}" "${new_descriptor}" code "${code}")
    # A byte destination may be unaligned; never promise struct alignment even
    # when access is through memcpy rather than a typed dereference.
    set(result_field "    RemapResult* wasm_result;")
    string(FIND "${code}" "${result_field}" result_field_at)
    if(result_field_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM remap destination field missing")
    endif()
    string(REPLACE "${result_field}" "    uint8_t* wasm_result;" code "${code}")
    set(old_write "    *rdata->wasm_result = *result;")
    set(old_read "    *result = *rdata->wasm_result;")
    string(FIND "${code}" "${old_write}" write_at)
    string(FIND "${code}" "${old_read}" read_at)
    if(write_at EQUAL -1 OR read_at EQUAL -1)
        message(FATAL_ERROR "Pinned WASM remap result transfer missing")
    endif()
    string(REPLACE "${old_write}" [=[    uint8_t encoded[sizeof(RemapResult)] = {0};
    encoded[0] = result->index;
    for (unsigned byte = 0; byte < 4; ++byte) {
        encoded[4 + byte] = (uint32_t)result->flip >> (byte * 8);
        encoded[8 + byte] = (uint32_t)result->rotate >> (byte * 8);
    }
    memcpy(rdata->wasm_result, encoded, sizeof encoded);]=] code "${code}")
    string(REPLACE "${old_read}" "    memcpy(result, rdata->wasm_result, sizeof *result);" code "${code}")
    set(${output} "${code}" PARENT_SCOPE)
endfunction()
