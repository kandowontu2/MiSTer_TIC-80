# Keep the pinned checkout untouched. Stage descriptor adaptations and narrow
# repairs to known API bugs while retaining the upstream interpreter sources.
include("${CMAKE_CURRENT_LIST_DIR}/runtime_wasm_memory.cmake")
function(tm_adapt_global_runtime target language source wrapper)
    if(source MATCHES "\\.cpp$")
        set(original "${TM_TIC80_SOURCE}/src/api/${source}")
    else()
        set(original "${TM_TIC80_SOURCE}/src/api/${source}.c")
    endif()
    file(READ "${original}" code)
    foreach(binding IN LISTS ARGN)
        string(REPLACE ":" ";" pair "${binding}")
        list(GET pair 0 upstream)
        list(GET pair 1 adapter)
        # Match the descriptor's assignment, never a function definition/call.
        string(REGEX MATCH "= +${upstream}," found "${code}")
        if(NOT found)
            message(FATAL_ERROR "Pinned ${language} adapter binding missing: ${upstream}")
        endif()
        string(REPLACE "${found}" "= ${adapter}," code "${code}")
    endforeach()
    if(language STREQUAL "python")
        # py_getdict returns a pointer into the same dictionary that py_setdict
        # can rehash. Copy the easing function before adding its alias.
        set(easing_alias "#define DEF_EASE(x) py_setdict(mod, py_name(\"Ease\" #x), py_getdict(mod, py_name(#x)));")
        string(FIND "${code}" "${easing_alias}" easing_alias_at)
        if(easing_alias_at EQUAL -1)
            message(FATAL_ERROR "Pinned Python easing alias binding missing")
        endif()
        string(REPLACE "${easing_alias}"
            "#define DEF_EASE(x) do { py_TValue value = *py_getdict(mod, py_name(#x)); py_setdict(mod, py_name(\"Ease\" #x), &value); } while(0);"
            code "${code}")
    endif()
    if(language STREQUAL "forth")
        file(SHA256 "${original}" forth_original_sha)
        if(NOT forth_original_sha STREQUAL "e94af80e9cc7d3c0f89dbfbd6256f6e14b3c398e9ca647b81d20deeacefa6e41")
            message(FATAL_ERROR "Pinned Forth SFX bounds repair source changed")
        endif()
        set(sfx_call "    s32 id       = (s32)pfPopFromStack();\n    gForthCore->api.sfx((tic_mem*)gForthCore,")
        string(FIND "${code}" "${sfx_call}" sfx_call_at)
        if(sfx_call_at EQUAL -1)
            message(FATAL_ERROR "Pinned Forth SFX call binding missing")
        endif()
        # Consume the word's arguments, report a cartridge error, and return
        # without indexing the sound engine. All negative IDs remain stops.
        string(REPLACE "${sfx_call}"
            "    s32 id       = (s32)pfPopFromStack();\n    const char* problem = id >= SFX_COUNT ? \"invalid sfx index\"\n        : (channel < 0 || channel >= TIC_SOUND_CHANNELS) ? \"invalid channel\" : NULL;\n    if (problem)\n    {\n        if (gForthCore->data) gForthCore->data->error(gForthCore->data->data, problem);\n        return 0;\n    }\n    gForthCore->api.sfx((tic_mem*)gForthCore," code "${code}")
    endif()
    if(language STREQUAL "wasm")
        file(SHA256 "${original}" wasm_original_sha)
        if(NOT wasm_original_sha STREQUAL "a7970b9a91fb9e6ff9c742932977661d29e3d441f7684877df534097cb8124f1")
            message(FATAL_ERROR "Pinned WASM SFX bounds repair source changed")
        endif()
        set(sfx_call "    if (channel >= 0 && channel < TIC_SOUND_CHANNELS)\n    {\n        core->api.sfx(tic, sfx_id, note, octave, duration, channel, volumeLeft & 0xf, volumeRight & 0xf, speed);")
        string(FIND "${code}" "${sfx_call}" sfx_call_at)
        if(sfx_call_at EQUAL -1)
            message(FATAL_ERROR "Pinned WASM SFX call binding missing")
        endif()
        # The core API indexes effect storage directly. Match the music import's
        # trap for invalid nonnegative IDs; preserve negative IDs as stops.
        string(REPLACE "${sfx_call}"
            "    if (sfx_id >= SFX_COUNT)\n        m3ApiTrap(\"invalid sfx index\");\n\n${sfx_call}" code "${code}")
        # wasm3 writes sizeof the declared C return type. These imports promise
        # i32, so a byte write leaves stale high bits in a reused result slot.
        foreach(return_api IN ITEMS peek peek4 peek2 peek1 pix vbank)
            string(REGEX MATCH "m3ApiRawFunction\\(wasmtic_${return_api}\\)[ \t\r\n]+\\{[ \t\r\n]+m3ApiReturnType[ \t]*\\((int8_t|u8)\\)" narrow_return "${code}")
            if(NOT narrow_return)
                message(FATAL_ERROR "Pinned WASM ${return_api} result binding missing")
            endif()
            string(REGEX REPLACE "m3ApiReturnType[ \t]*\\((int8_t|u8)\\)" "m3ApiReturnType(int32_t)" wide_return "${narrow_return}")
            string(REPLACE "${narrow_return}" "${wide_return}" code "${code}")
        endforeach()
        # The public mouse result contains nine bytes. Check its integer offset
        # before constructing a host pointer, and permit unaligned WASM memory.
        string(REGEX MATCH "m3ApiRawFunction\\(wasmtic_mouse\\)[^{]*\\{[^}]*\\};[^}]*\\}" mouse_import "${code}")
        if(NOT mouse_import)
            message(FATAL_ERROR "Pinned WASM mouse destination binding missing")
        endif()
        set(checked_mouse_import [=[m3ApiRawFunction(wasmtic_mouse)
{
    m3ApiGetArg(uint32_t, destination);
    uint32_t memory_size = m3_GetMemorySize(runtime);
    if (memory_size < 9 || destination > memory_size - 9)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    tic_core* core = getWasmCore(runtime); tic_mem* tic = (tic_mem*)core;
    const tic80_mouse* mouse = &tic->ram->input.mouse;
    tic_point pos = core->api.mouse(tic);
    uint16_t x = (uint16_t)pos.x, y = (uint16_t)pos.y;
    uint8_t data[9] = {x & 0xff, x >> 8, y & 0xff, y >> 8,
        (uint8_t)mouse->scrollx, (uint8_t)mouse->scrolly,
        mouse->left, mouse->middle, mouse->right};
    memcpy(m3ApiOffsetToPtr(destination), data, sizeof data);
    m3ApiSuccess();
}]=])
        string(REPLACE "${mouse_import}" "${checked_mouse_import}" code "${code}")
        tm_repair_wasm_pointer_imports("${code}" code)
    endif()
    if(language STREQUAL "janet")
        file(SHA256 "${original}" janet_original_sha)
        if(NOT janet_original_sha STREQUAL "1caed296c38b7a0d2c5ffea6930890603e6f4bdc61b3ac5634a288b03d6666aa")
            message(FATAL_ERROR "Pinned Janet SFX bounds repair source changed")
        endif()
        # tic_key is a byte: -1 becomes 255, never the core's any-key sentinel.
        string(REGEX MATCHALL "tic_key key = -1" wrong_default_keys "${code}")
        list(LENGTH wrong_default_keys wrong_default_key_count)
        if(NOT wrong_default_key_count EQUAL 2)
            message(FATAL_ERROR "Pinned Janet any-key bindings missing")
        endif()
        string(REPLACE "tic_key key = -1;" "tic_key key = tic_key_unknown;" code "${code}")
        foreach(diagnostic IN ITEMS "unknown sfx index, got" "unknown channel, got")
            string(FIND "${code}" "${diagnostic} %s" diagnostic_at)
            if(diagnostic_at EQUAL -1)
                message(FATAL_ERROR "Pinned Janet integer diagnostic missing: ${diagnostic}")
            endif()
            # Janet's %s reads a string pointer. These arguments are s32.
            string(REPLACE "${diagnostic} %s" "${diagnostic} %d" code "${code}")
        endforeach()
        set(channel_check "if (channel < 0 || channel > TIC_SOUND_CHANNELS)")
        string(FIND "${code}" "${channel_check}" channel_check_at)
        if(channel_check_at EQUAL -1)
            message(FATAL_ERROR "Pinned Janet SFX channel guard missing")
        endif()
        string(REPLACE "${channel_check}" "if (channel < 0 || channel >= TIC_SOUND_CHANNELS)" code "${code}")
        set(effect_defaults "    tic_sample* effect = tic->ram->sfx.samples.data + index;\n    SFXNote defaultSfxNote = {-1, -1};\n    s32 defaultSpeed = SFX_DEF_SPEED;\n    if (index >= 0)\n    {\n        defaultSfxNote.note = effect->note;")
        string(FIND "${code}" "${effect_defaults}" effect_defaults_at)
        if(effect_defaults_at EQUAL -1)
            message(FATAL_ERROR "Pinned Janet SFX default binding missing")
        endif()
        # Form an effect pointer only after establishing a valid nonnegative ID.
        string(REPLACE "${effect_defaults}"
            "    SFXNote defaultSfxNote = {-1, -1};\n    s32 defaultSpeed = SFX_DEF_SPEED;\n    if (index >= 0)\n    {\n        tic_sample* effect = tic->ram->sfx.samples.data + index;\n        defaultSfxNote.note = effect->note;" code "${code}")
        # Distinguish the smoothed FFT API even when capture is disabled.
        set(ffts_end "    return janet_wrap_number(core->api.fft(tic, start_freq, end_freq));\n}\n\n/* ***************** */\nstatic void reportError")
        string(FIND "${code}" "${ffts_end}" ffts_end_at)
        if(ffts_end_at EQUAL -1)
            message(FATAL_ERROR "Pinned Janet smoothed FFT dispatch binding missing")
        endif()
        string(REPLACE "${ffts_end}"
            "    return janet_wrap_number(core->api.ffts(tic, start_freq, end_freq));\n}\n\n/* ***************** */\nstatic void reportError" code "${code}")
    endif()
    if(language STREQUAL "wren")
        file(SHA256 "${original}" wren_original_sha)
        if(NOT wren_original_sha STREQUAL "f243df9c3aaa64247e181c24ceb3cc661b55fe7224caf2d41f738161fa599652")
            message(FATAL_ERROR "Pinned Wren SFX repair source changed")
        endif()
        # btn() addresses all four gamepads. Both btnp() overloads must keep
        # the same five-bit index instead of aliasing pads three/four to one/two.
        set(narrow_button_index "s32 index = getWrenNumber(vm, 1) & 0xf;")
        string(REGEX MATCHALL "s32 index = getWrenNumber\\(vm, 1\\) & 0xf" narrow_button_indices "${code}")
        list(LENGTH narrow_button_indices narrow_button_index_count)
        if(NOT narrow_button_index_count EQUAL 2)
            message(FATAL_ERROR "Pinned Wren four-gamepad btnp bindings missing")
        endif()
        string(REPLACE "${narrow_button_index}"
            "s32 index = getWrenNumber(vm, 1) & 0x1f;" code "${code}")
        # List elements need a rooted VM slot beyond the receiver/arguments.
        # Keep top as the original arity so the optional speed test is unchanged.
        set(stereo_list "if(isList(vm, 5) && wrenGetListCount(vm, 5) == COUNT_OF(volumes))\n                        {\n                            for(s32 i = 0; i < COUNT_OF(volumes); i++)")
        string(FIND "${code}" "${stereo_list}" stereo_list_at)
        if(stereo_list_at EQUAL -1)
            message(FATAL_ERROR "Pinned Wren stereo SFX binding missing")
        endif()
        string(REPLACE "${stereo_list}"
            "if(isList(vm, 5) && wrenGetListCount(vm, 5) == COUNT_OF(volumes))\n                        {\n                            wrenEnsureSlots(vm, top + 1);\n                            for(s32 i = 0; i < COUNT_OF(volumes); i++)"
            code "${code}")
        # The inherited empty OVR() method must not clear the modern video bank.
        set(overline_guard "if(overline_handle)")
        string(REGEX MATCHALL "if\\(overline_handle\\)" overline_guards "${code}")
        list(LENGTH overline_guards overline_guard_count)
        if(NOT overline_guard_count EQUAL 1)
            message(FATAL_ERROR "Pinned Wren overline guard missing")
        endif()
        string(REPLACE "${overline_guard}" "if(overline_handle && tm_wren_has_overline(tic))" code "${code}")
    endif()
    if(language STREQUAL "ruby")
        file(SHA256 "${original}" ruby_original_sha)
        if(NOT ruby_original_sha STREQUAL "c51e3058f6e9c98c042fdfbe1e161045b8a010e04ddd68bcc63ee94773d76dce")
            message(FATAL_ERROR "Pinned Ruby SFX repair source changed")
        endif()
        foreach(key_bound IN ITEMS "if(key < tic_key_escape)" "else if (key >= tic_key_escape)"
                "return mrb_fixnum_value(core->api.keyp(tic, -1, -1, -1));")
            string(FIND "${code}" "${key_bound}" key_bound_at)
            if(key_bound_at EQUAL -1)
                message(FATAL_ERROR "Pinned Ruby keyboard binding missing: ${key_bound}")
            endif()
        endforeach()
        # Escape, function and keypad keys are valid in the current key enum.
        string(REPLACE "if(key < tic_key_escape)"
            "if(key >= tic_key_unknown && key < tic_keys_count)" code "${code}")
        string(REPLACE "else if (key >= tic_key_escape)"
            "else if (key < tic_key_unknown || key >= tic_keys_count)" code "${code}")
        # Ruby treats numeric zero as true: keyp() must return an actual boolean.
        string(REPLACE "return mrb_fixnum_value(core->api.keyp(tic, -1, -1, -1));"
            "return mrb_bool_value(core->api.keyp(tic, tic_key_unknown, -1, -1));" code "${code}")
        # mrb_get_args already parsed the sixth argument. Use the preset speed
        # only when that argument was omitted.
        string(REGEX MATCHALL "speed = effect->speed" preset_speeds "${code}")
        list(LENGTH preset_speeds preset_speed_count)
        if(NOT preset_speed_count EQUAL 1)
            message(FATAL_ERROR "Pinned Ruby SFX speed binding missing")
        endif()
        string(REPLACE "speed = effect->speed;" "if(argc < 6) speed = effect->speed;" code "${code}")
    endif()
    if(language STREQUAL "miniscript")
        file(SHA256 "${original}" miniscript_original_sha)
        if(NOT miniscript_original_sha STREQUAL "8c86a48bb4de693232fee0fed0304b8497e37546b59b32b8a6e931804981e487")
            message(FATAL_ERROR "Pinned MiniScript SFX defaults repair source changed")
        endif()
        set(effect_defaults "        if (index > 0) {\n            tic_sample* effect = tic->ram->sfx.samples.data + index;")
        string(FIND "${code}" "${effect_defaults}" effect_defaults_at)
        if(effect_defaults_at EQUAL -1)
            message(FATAL_ERROR "Pinned MiniScript SFX preset binding missing")
        endif()
        # Effect zero has preset pitch and speed just like the other 63 effects.
        string(REPLACE "${effect_defaults}"
            "        if (index >= 0) {\n            tic_sample* effect = tic->ram->sfx.samples.data + index;" code "${code}")
        # Specific keyp() overloads mistakenly call the gamepad API. Besides
        # false controller-triggered events, high keycodes can index beyond
        # its 32-button repeat array. Both overloads must use keyboard state.
        set(wrong_keyp "core->api.btnp(tic, code,")
        string(REGEX MATCHALL "core->api.btnp\\(tic, code," wrong_keyp_calls "${code}")
        list(LENGTH wrong_keyp_calls wrong_keyp_count)
        if(NOT wrong_keyp_count EQUAL 2)
            message(FATAL_ERROR "Pinned MiniScript keyp repair bindings missing")
        endif()
        string(REPLACE "${wrong_keyp}" "core->api.keyp(tic, code," code "${code}")
    endif()
    tm_patch_sfx_defaults(${language} code)
    tm_patch_fft_bindings(${language} code)
    if(language STREQUAL "miniscript")
        tm_patch_miniscript_config(code)
    endif()
    if(language STREQUAL "forth")
        tm_patch_forth_stack(code)
    endif()
    string(REPLACE "\"../build/assets/" "\"${TM_TIC80_SOURCE}/build/assets/" code "${code}")
    set(generated "${CMAKE_BINARY_DIR}/runtime_adapters/${language}.inc")
    tm_write_generated("${generated}" "${code}")
    get_target_property(sources ${target} SOURCES)
    list(REMOVE_ITEM sources "${original}")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    target_sources(${target} PRIVATE "${wrapper}")
    target_compile_definitions(${target} PRIVATE TM_UPSTREAM_ADAPTER="${generated}")
endfunction()
