# Reuse the pinned upstream core build recipes without configuring the desktop
# studio or SDL. Upstream recipes resolve sources via CMAKE_SOURCE_DIR; override
# it only in this function's scope, leaving the parent project's paths intact.
function(tm_write_generated path contents)
    if(EXISTS "${path}")
        file(READ "${path}" previous)
        if(previous STREQUAL contents)
            return()
        endif()
    endif()
    file(WRITE "${path}" "${contents}")
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/runtime_sfx_defaults.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/runtime_fft.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/runtime_forth_stack.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/runtime_cpp_compat.cmake")

function(tm_add_runtime)
    set(TM_INTEGRATION_SOURCE "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${TM_TIC80_SOURCE}/vendor/lua/lua.h")
        message(FATAL_ERROR "Initialize TIC-80 dependencies using tools/bootstrap.py")
    endif()
    set(CMAKE_SOURCE_DIR "${TM_TIC80_SOURCE}")
    set(THIRDPARTY_DIR "${TM_TIC80_SOURCE}/vendor")
    set(BUILD_STATIC ON)
    set(TIC_RUNTIME STATIC)
    set(BUILD_WITH_LUA ON)
    foreach(language JS MOON YUE FENNEL SCHEME SQUIRREL PYTHON WREN JANET WASM RUBY MINISCRIPT FORTH)
        set(BUILD_WITH_${language} ${TM_BUILD_EXTENDED_RUNTIME})
    endforeach()
    set(BUILD_WITH_ZLIB ON)
    set(PREFER_SYSTEM_LIBRARIES OFF)
    include("${TM_TIC80_SOURCE}/cmake/version.cmake")
    configure_file("${TM_TIC80_SOURCE}/version.h.in" "${CMAKE_BINARY_DIR}/version.h")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(LINUX TRUE)
    endif()
    add_library(runtime INTERFACE)
    target_compile_definitions(runtime INTERFACE TIC_RUNTIME_STATIC BUILD_DEPRECATED)
    # Upstream's embedded-player configuration disables OS module discovery.
    # All supported languages are linked into this executable; never dlopen a
    # host module from a cartridge tag (also avoids static-glibc ABI problems).
    target_compile_definitions(runtime INTERFACE __LIBRETRO__)
    # Audio input is opened only by explicitly configured workers. Linux builds
    # link the repaired ALSA/DSP path after the upstream core recipe is staged.
    if(NOT TM_ENABLE_FFT_CAPTURE)
        target_compile_definitions(runtime INTERFACE TIC80_FFT_UNSUPPORTED)
    endif()
    include("${TM_TIC80_SOURCE}/cmake/gif.cmake")
    include("${TM_TIC80_SOURCE}/cmake/blipbuf.cmake")
    include("${TM_TIC80_SOURCE}/cmake/zlib.cmake")
    include("${TM_TIC80_SOURCE}/cmake/png.cmake")
    include("${TM_TIC80_SOURCE}/cmake/lua.cmake")
    # lua_rawgeti pushes the element but returns its type tag, not its value.
    # Preserve stereo volume values, including zero, in all Lua-family runtimes.
    set(original_luaapi "${TM_TIC80_SOURCE}/src/api/luaapi.c")
    file(SHA256 "${original_luaapi}" luaapi_original_sha)
    if(NOT luaapi_original_sha STREQUAL "7d4ae316897d8a38e9024ebe41fc8ea467ce17669668a4fbe9130f4f4b23418d")
        message(FATAL_ERROR "Pinned Lua stereo SFX repair source changed")
    endif()
    file(READ "${original_luaapi}" luaapi_code)
    set(stereo_read "                                    volumes[i] = lua_rawgeti(lua, 5, i + 1);")
    string(FIND "${luaapi_code}" "${stereo_read}" stereo_read_at)
    if(stereo_read_at EQUAL -1)
        message(FATAL_ERROR "Pinned Lua stereo SFX volume binding missing")
    endif()
    string(REPLACE "${stereo_read}"
        "                                    lua_rawgeti(lua, 5, i + 1);\n                                    volumes[i] = getLuaNumber(lua, -1);" luaapi_code "${luaapi_code}")
    tm_patch_sfx_defaults(luaapi luaapi_code)
    tm_patch_fft_bindings(luaapi luaapi_code)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime_adapters")
    set(patched_luaapi "${CMAKE_BINARY_DIR}/runtime_adapters/luaapi.c")
    tm_write_generated("${patched_luaapi}" "${luaapi_code}")
    get_target_property(luaapi_sources luaapi SOURCES)
    list(REMOVE_ITEM luaapi_sources "${original_luaapi}")
    set_property(TARGET luaapi PROPERTY SOURCES "${luaapi_sources}")
    target_sources(luaapi PRIVATE "${patched_luaapi}")
    if(TM_BUILD_EXTENDED_RUNTIME)
        foreach(recipe quickjs moon yue fennel scheme squirrel pocketpy wren janet wasm)
            include("${TM_TIC80_SOURCE}/cmake/${recipe}.cmake")
        endforeach()
        # Four unused permanent strings are allocated during s7's one-time
        # initialization and lose their only roots under optimization. Omit
        # only these dead allocations, retaining the pinned implementation.
        set(original_scheme "${THIRDPARTY_DIR}/s7/s7.c")
        file(SHA256 "${original_scheme}" scheme_engine_sha)
        if(NOT scheme_engine_sha STREQUAL "b06c6402cf1062a34d1626b91d2934cc1c045d261d26cecd2f73ab2371cf59c6")
            message(FATAL_ERROR "Pinned s7 stacktrace ownership repair source changed")
        endif()
        file(READ "${original_scheme}" scheme_code)
        foreach(port input_string input_file output_string output_file)
            string(REGEX MATCH "  an_${port}_port_string = +make_permanent_string\([^\n]+\);" unused_port "${scheme_code}")
            if(NOT unused_port)
                message(FATAL_ERROR "Pinned s7 unused ${port} string binding missing")
            endif()
            string(REPLACE "${unused_port}" "  /* Unused permanent ${port} port string omitted. */" scheme_code "${scheme_code}")
        endforeach()
        # The error-handler frame copies its notes into a managed output block.
        # Release the walker's heap string, as the regular-frame path does.
        set(error_frame "\t  strp = stacktrace_add_func(sc, f, err_code, string_value(errstr), notes, code_cols, as_comment);\n\t  str = (char *)block_data(strp);")
        string(FIND "${scheme_code}" "${error_frame}" error_frame_at)
        if(error_frame_at EQUAL -1)
            message(FATAL_ERROR "Pinned s7 error-frame note ownership binding missing")
        endif()
        string(REPLACE "${error_frame}"
            "\t  strp = stacktrace_add_func(sc, f, err_code, string_value(errstr), notes, code_cols, as_comment);\n\t  free(notes);\n\t  str = (char *)block_data(strp);" scheme_code "${scheme_code}")
        set(patched_scheme "${CMAKE_BINARY_DIR}/runtime_adapters/s7.c")
        tm_write_generated("${patched_scheme}" "${scheme_code}")
        get_target_property(scheme_sources scheme SOURCES)
        list(REMOVE_ITEM scheme_sources "${original_scheme}")
        set_property(TARGET scheme PROPERTY SOURCES "${scheme_sources}")
        target_sources(scheme PRIVATE "${patched_scheme}")
        get_target_property(wasm_sources wasm SOURCES)
        list(REMOVE_ITEM wasm_sources "${THIRDPARTY_DIR}/wasm3/source/m3_module.c")
        set_property(TARGET wasm PROPERTY SOURCES "${wasm_sources}")
        target_sources(wasm PRIVATE "${TM_INTEGRATION_SOURCE}/src/runtime_wasm_module.c")
        target_compile_definitions(wasm PRIVATE
            TM_UPSTREAM_WASM_MODULE="${THIRDPARTY_DIR}/wasm3/source/m3_module.c")
        include("${TM_INTEGRATION_SOURCE}/cmake/runtime_wasm_env.cmake")
        include("${TM_INTEGRATION_SOURCE}/cmake/runtime_wasm_parse.cmake")
        include("${TM_INTEGRATION_SOURCE}/cmake/runtime_ruby.cmake")
        include("${TM_INTEGRATION_SOURCE}/cmake/runtime_forth.cmake")
        # The upstream MiniScript recipe uses two relative integration paths.
        # Make those absolute when including it from this parent project.
        file(READ "${TM_TIC80_SOURCE}/cmake/miniscript.cmake" miniscript_recipe)
        string(REPLACE "src/api/miniscript.cpp src/api/parse_note.c"
            "\"${TM_TIC80_SOURCE}/src/api/miniscript.cpp\" \"${TM_TIC80_SOURCE}/src/api/parse_note.c\""
            miniscript_recipe "${miniscript_recipe}")
        tm_write_generated("${CMAKE_BINARY_DIR}/runtime_adapters/miniscript.cmake" "${miniscript_recipe}")
        include("${CMAKE_BINARY_DIR}/runtime_adapters/miniscript.cmake")
        include("${TM_INTEGRATION_SOURCE}/cmake/runtime_adapters.cmake")
        tm_adapt_global_runtime(python python python "${TM_INTEGRATION_SOURCE}/src/runtime_python.c"
            init_pkpy_v2:tm_python_init close_pkpy_v2:tm_python_close
            tick_pkpy_v2:tm_python_tick boot_pkpy_v2:tm_python_boot
            callback_scanline:tm_python_scn callback_border:tm_python_bdr
            callback_menu:tm_python_menu eval_pkpy_v2:tm_python_eval)
        tm_adapt_global_runtime(janet janet janet "${TM_INTEGRATION_SOURCE}/src/runtime_janet.c"
            initJanet:tm_janet_init closeJanet:tm_janet_close
            callJanetTick:tm_janet_tick callJanetBoot:tm_janet_boot
            callJanetScanline:tm_janet_scn callJanetBorder:tm_janet_bdr
            callJanetMenu:tm_janet_menu evalJanet:tm_janet_eval)
        tm_adapt_global_runtime(wasm wasm wasm "${TM_INTEGRATION_SOURCE}/src/runtime_wasm.c"
            initWasm:tm_wasm_init closeWasm:tm_wasm_close
            callWasmTick:tm_wasm_tick callWasmBoot:tm_wasm_boot
            callWasmScanline:tm_wasm_scn callWasmBorder:tm_wasm_bdr
            callWasmMenu:tm_wasm_menu)
        tm_adapt_global_runtime(wren wren wren "${TM_INTEGRATION_SOURCE}/src/runtime_wren.c"
            initWren:tm_wren_init closeWren:tm_wren_close
            callWrenTick:tm_wren_tick callWrenBoot:tm_wren_boot
            callWrenScanline:tm_wren_scn callWrenBorder:tm_wren_bdr
            callWrenMenu:tm_wren_menu)
        tm_adapt_global_runtime(forth forth forth "${TM_INTEGRATION_SOURCE}/src/runtime_forth.c"
            initForth:tm_forth_init closeForth:tm_forth_close
            callForthTick:tm_forth_tick callForthBoot:tm_forth_boot
            callForthScanline:tm_forth_scn callForthBorder:tm_forth_bdr
            callForthMenu:tm_forth_menu evalForth:tm_forth_eval)
        tm_adapt_global_runtime(ruby ruby mruby "${TM_INTEGRATION_SOURCE}/src/runtime_ruby.c"
            initMRuby:tm_ruby_init closeMRuby:tm_ruby_close
            callMRubyTick:tm_ruby_tick callMRubyBoot:tm_ruby_boot
            callMRubyScanline:tm_ruby_scn callMRubyBorder:tm_ruby_bdr
            callMRubyMenu:tm_ruby_menu evalMRuby:tm_ruby_eval)
        tm_adapt_global_runtime(miniscript miniscript miniscript.cpp "${TM_INTEGRATION_SOURCE}/src/runtime_miniscript.cpp"
            callMiniScriptScanline:tm_miniscript_scn callMiniScriptBorder:tm_miniscript_bdr
            callMiniScriptMenu:tm_miniscript_menu)
        tm_stage_squirrel_sfx_defaults()
    endif()
    include("${TM_INTEGRATION_SOURCE}/cmake/runtime_notes.cmake")
    tm_stage_runtime_notes()
    include("${TM_TIC80_SOURCE}/cmake/core.cmake")
    # The pinned serializer writes CHUNK_LANG but omits it from the returned
    # length. Retain the explicit language ID for code without a script tag.
    # Stage this one-line fix rather than modifying the reference checkout.
    set(original_cart "${TM_TIC80_SOURCE}/src/cart.c")
    file(READ "${original_cart}" cart_code)
    set(language_chunk "        SAVE_CHUNK(CHUNK_LANG, cart->lang, 0);")
    string(FIND "${cart_code}" "${language_chunk}" language_chunk_at)
    if(language_chunk_at EQUAL -1)
        message(FATAL_ERROR "Pinned cartridge language serializer binding missing")
    endif()
    string(REPLACE "${language_chunk}" "        buffer = SAVE_CHUNK(CHUNK_LANG, cart->lang, 0);" cart_code "${cart_code}")
    set(patched_cart "${CMAKE_BINARY_DIR}/runtime_adapters/cart.c")
    tm_write_generated("${patched_cart}" "${cart_code}")
    # File extensions arrive with their original case from MiSTer's browser.
    # Guard short names before suffix arithmetic, then compare the suffix only.
    file(READ "${TM_TIC80_SOURCE}/src/tools.c" tools_code)
    set(extension_check "    return strcmp(name + strlen(name) - strlen(ext), ext) == 0;")
    string(FIND "${tools_code}" "${extension_check}" extension_check_at)
    if(extension_check_at LESS 0)
        message(FATAL_ERROR "Pinned extension predicate changed")
    endif()
    string(REPLACE "${extension_check}" [=[    if(!name || !ext) return false;
    size_t ns = strlen(name), es = strlen(ext);
    if(ns < es) return false;
    const unsigned char* suffix = (const unsigned char*)name + ns - es;
    const unsigned char* ending = (const unsigned char*)ext;
    for(size_t i = 0; i < es; ++i)
        if(tolower(suffix[i]) != tolower(ending[i])) return false;
    return true;]=] tools_code "${tools_code}")
    set(patched_tools "${CMAKE_BINARY_DIR}/runtime_adapters/tools.c")
    tm_write_generated("${patched_tools}" "${tools_code}")
    # Failed initialization can leave an allocated VM. Close it before losing
    # its pointer, using the same ownership and RAM restoration as a normal
    # shutdown. Preserve the pinned source and its license in build staging.
    file(READ "${TM_TIC80_SOURCE}/src/core/core.c" core_code)
    set(failed_vm "        // if it couldn't init, make sure the VM is not left dirty by the implementation\n        core->currentVM = NULL;")
    string(FIND "${core_code}" "${failed_vm}" failed_vm_at)
    if(failed_vm_at EQUAL -1)
        message(FATAL_ERROR "Pinned failed-VM cleanup binding missing")
    endif()
    string(REPLACE "${failed_vm}"
        "        // Close a partially initialized VM before releasing its pointer.\n        tic_close_current_vm(core);"
        core_code "${core_code}")
    set(patched_core "${CMAKE_BINARY_DIR}/runtime_adapters/core.inc")
    tm_write_generated("${patched_core}" "${core_code}")
    get_target_property(core_sources tic80core SOURCES)
    list(REMOVE_ITEM core_sources "${original_cart}")
    list(REMOVE_ITEM core_sources "${TM_TIC80_SOURCE}/src/tools.c")
    list(REMOVE_ITEM core_sources "${TM_TIC80_SOURCE}/src/tic.c")
    list(REMOVE_ITEM core_sources "${TM_TIC80_SOURCE}/src/core/core.c")
    set_property(TARGET tic80core PROPERTY SOURCES "${core_sources}")
    target_sources(tic80core PRIVATE "${patched_cart}")
    target_sources(tic80core PRIVATE "${patched_tools}")
    target_sources(tic80core PRIVATE "${TM_INTEGRATION_SOURCE}/src/runtime_tick.c")
    target_sources(tic80core PRIVATE "${TM_INTEGRATION_SOURCE}/src/runtime_core.c")
    target_compile_definitions(tic80core PRIVATE
        TM_UPSTREAM_PLAYER="${TM_TIC80_SOURCE}/src/tic.c"
        TM_UPSTREAM_CORE="${patched_core}")
    target_include_directories(tic80core PRIVATE "${TM_TIC80_SOURCE}/src/core")
    target_link_libraries(png PUBLIC zlib)
    set_target_properties(tic80core lua luaapi PROPERTIES C_STANDARD 11)
endfunction()
