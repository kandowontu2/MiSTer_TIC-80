# Transform only the SFX binding body, preserving unrelated APIs and the pinned
# checkout. Numeric -1 means preset pitch; an explicit valid speed stays explicit.
function(tm_sfx_defaults_replace variable before after)
    string(FIND "${${variable}}" "${before}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Pinned SFX defaults binding missing: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" result "${${variable}}")
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()

function(tm_patch_sfx_defaults language variable)
    set(file "${language}.c")
    if(language STREQUAL "ruby")
        set(file "mruby.c")
    elseif(language STREQUAL "miniscript")
        set(file "miniscript.cpp")
    endif()
    set(pins
        "luaapi:7d4ae316897d8a38e9024ebe41fc8ea467ce17669668a4fbe9130f4f4b23418d"
        "js:129bc80e3402020f4da61758215c32d92503b15f31e7aa92e290f406ff3ce520"
        "scheme:e882e519cbaba685b9e1dc2b178a3885306c36736effdb3f92f40bca2c399e52"
        "squirrel:3bba3f35bc3818e8093324abdb10d9abc2c66f79f774e05732d19b646ef1876a"
        "python:133ee9d15c7d2b024a0b56c52bd53b8a4854cdb5ddb0f25ce71caf3fd312611d"
        "wren:f243df9c3aaa64247e181c24ceb3cc661b55fe7224caf2d41f738161fa599652"
        "janet:1caed296c38b7a0d2c5ffea6930890603e6f4bdc61b3ac5634a288b03d6666aa"
        "ruby:c51e3058f6e9c98c042fdfbe1e161045b8a010e04ddd68bcc63ee94773d76dce"
        "miniscript:8c86a48bb4de693232fee0fed0304b8497e37546b59b32b8a6e931804981e487"
        "forth:e94af80e9cc7d3c0f89dbfbd6256f6e14b3c398e9ca647b81d20deeacefa6e41"
        "wasm:a7970b9a91fb9e6ff9c742932977661d29e3d441f7684877df534097cb8124f1")
    set(expected)
    foreach(pin IN LISTS pins)
        if(pin MATCHES "^${language}:(.+)$")
            set(expected "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    file(SHA256 "${TM_TIC80_SOURCE}/src/api/${file}" actual)
    if(NOT expected OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "Pinned ${language} SFX defaults source changed")
    endif()
    set(code "${${variable}}")
    if(language STREQUAL "miniscript")
        tm_sfx_defaults_replace(code "            s32 n = noteValue.IntValue();\n            note = n % NOTES;\n            octave = n / NOTES;"
            "            s32 n = noteValue.IntValue();\n            if (n != -1) {\n                note = n % NOTES;\n                octave = n / NOTES;\n            }")
        set(${variable} "${code}" PARENT_SCOPE)
        return()
    endif()
    if(language STREQUAL "luaapi")
        set(marker "static s32 lua_sfx(")
    elseif(language STREQUAL "js")
        set(marker "static JSValue js_sfx(")
    elseif(language STREQUAL "scheme")
        set(marker "s7_pointer scheme_sfx(")
    elseif(language STREQUAL "squirrel")
        set(marker "static SQInteger squirrel_sfx(")
    elseif(language STREQUAL "python")
        set(marker "static bool py_sfx(")
    elseif(language STREQUAL "wren")
        set(marker "static void wren_sfx(")
    elseif(language STREQUAL "ruby")
        set(marker "static mrb_value mrb_sfx(")
    elseif(language STREQUAL "janet")
        set(marker "static Janet janet_sfx(int32_t argc, Janet* argv)\n{")
    elseif(language STREQUAL "forth")
        set(marker "static cell_t tic_forth_sfx(")
    elseif(language STREQUAL "wasm")
        set(marker "m3ApiRawFunction(wasmtic_sfx)")
    endif()
    string(FIND "${code}" "${marker}" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "Pinned ${language} SFX function missing")
    endif()
    string(SUBSTRING "${code}" ${start} -1 tail)
    string(FIND "${tail}" "\n}\n" end)
    if(end EQUAL -1)
        message(FATAL_ERROR "Pinned ${language} SFX function end missing")
    endif()
    math(EXPR length "${end} + 3")
    string(SUBSTRING "${tail}" 0 ${length} body)
    if(language STREQUAL "luaapi")
        tm_sfx_defaults_replace(body "                    note = id % NOTES;\n                    octave = id / NOTES;"
            "                    if (id != -1)\n                    {\n                        note = id % NOTES;\n                        octave = id / NOTES;\n                    }")
    elseif(language STREQUAL "js")
        tm_sfx_defaults_replace(body "if(!JS_IsUndefined(argv[1]))" "if(!JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))")
        tm_sfx_defaults_replace(body "                    note = id % NOTES;\n                    octave = id / NOTES;"
            "                    if (id != -1)\n                    {\n                        note = id % NOTES;\n                        octave = id / NOTES;\n                    }")
    elseif(language STREQUAL "squirrel")
        tm_sfx_defaults_replace(body "if(sq_gettype(vm, 3) & (OT_INTEGER|OT_FLOAT))"
            "if(sq_gettype(vm, 3) == OT_INTEGER || sq_gettype(vm, 3) == OT_FLOAT)")
        tm_sfx_defaults_replace(body "                    note = id % NOTES;\n                    octave = id / NOTES;"
            "                    if (id != -1)\n                    {\n                        note = id % NOTES;\n                        octave = id / NOTES;\n                    }")
    elseif(language STREQUAL "wren")
        tm_sfx_defaults_replace(body "                note = id % NOTES;\n                octave = id / NOTES;"
            "                if (id != -1)\n                {\n                    note = id % NOTES;\n                    octave = id / NOTES;\n                }")
    elseif(language STREQUAL "ruby")
        tm_sfx_defaults_replace(body "        if (argc >= 2)" "        if (argc >= 2 && !mrb_nil_p(note_obj))")
        tm_sfx_defaults_replace(body "                note = id % NOTES;\n                octave = id / NOTES;"
            "                if (id != -1)\n                {\n                    note = id % NOTES;\n                    octave = id / NOTES;\n                }")
    elseif(language STREQUAL "scheme")
        tm_sfx_defaults_replace(body "    int note = -1;\n    int octave = -1;"
            "    int note = -1;\n    int octave = -1;\n    s32 default_speed = SFX_DEF_SPEED;\n    if (id >= 0) {\n        const tic_sample* effect = tic->ram->sfx.samples.data + id;\n        note = effect->note;\n        octave = effect->octave;\n        default_speed = effect->speed;\n    }")
        tm_sfx_defaults_replace(body "if (raw_note >= 0 || raw_note <= 95)" "if (raw_note != -1)")
        tm_sfx_defaults_replace(body "const s32 speed = argn > 5 ? s7_integer(s7_list_ref(sc, args, 5)) : 0;"
            "const s32 speed = argn > 5 ? s7_integer(s7_list_ref(sc, args, 5)) : default_speed;")
    elseif(language STREQUAL "python")
        tm_sfx_defaults_replace(body "    PY_CHECK_ARG_TYPE(5, tp_int);" "    if (!py_isnone(py_arg(5))) { PY_CHECK_ARG_TYPE(5, tp_int); }")
        tm_sfx_defaults_replace(body "    s32 speed = py_toint(py_arg(5));" "    s32 speed = py_isnone(py_arg(5)) ? SFX_DEF_SPEED : py_toint(py_arg(5));")
        tm_sfx_defaults_replace(body "if (speed == -1) speed = effect->speed;" "if (speed == SFX_DEF_SPEED) speed = effect->speed;")
        tm_sfx_defaults_replace(body "    else\n    {\n        PY_CHECK_ARG_TYPE(1, tp_int);" "    else if (!py_isnone(py_arg(1)))\n    {\n        PY_CHECK_ARG_TYPE(1, tp_int);")
    elseif(language STREQUAL "forth")
        tm_sfx_defaults_replace(body "    gForthCore->api.sfx((tic_mem*)gForthCore,"
            "    if (id >= 0 && note == -1 && octave == -1)\n    {\n        const tic_sample* effect = ((tic_mem*)gForthCore)->ram->sfx.samples.data + id;\n        note = effect->note;\n        octave = effect->octave;\n    }\n    gForthCore->api.sfx((tic_mem*)gForthCore,")
    elseif(language STREQUAL "wasm")
        tm_sfx_defaults_replace(body "        core->api.sfx(tic, sfx_id, note, octave, duration, channel, volumeLeft & 0xf, volumeRight & 0xf, speed);"
            "        if (sfx_id >= 0 && note == -1 && octave == -1)\n        {\n            const tic_sample* effect = tic->ram->sfx.samples.data + sfx_id;\n            note = effect->note;\n            octave = effect->octave;\n        }\n        core->api.sfx(tic, sfx_id, note, octave, duration, channel, volumeLeft & 0xf, volumeRight & 0xf, speed);")
    endif()
    string(SUBSTRING "${code}" 0 ${start} prefix)
    math(EXPR finish "${start} + ${length}")
    string(SUBSTRING "${code}" ${finish} -1 suffix)
    set(code "${prefix}${body}${suffix}")
    if(language STREQUAL "janet")
        tm_sfx_defaults_replace(code "    if (argc <= n)\n    {\n        return sfxNote;"
            "    if (argc <= n || janet_checktype(argv[n], JANET_NIL))\n    {\n        return sfxNote;")
        tm_sfx_defaults_replace(code "        sfxNote.note = id % NOTES;\n        sfxNote.octave = id / NOTES;"
            "        if (id != -1)\n        {\n            sfxNote.note = id % NOTES;\n            sfxNote.octave = id / NOTES;\n        }")
    elseif(language STREQUAL "python")
        tm_sfx_defaults_replace(code "sfx(id: int, note=-1, duration=-1, channel=0, volume=15, speed=-1)"
            "sfx(id: int, note=-1, duration=-1, channel=0, volume=15, speed=None)")
    endif()
    set(${variable} "${code}" PARENT_SCOPE)
endfunction()

function(tm_stage_squirrel_sfx_defaults)
    set(original "${TM_TIC80_SOURCE}/src/api/squirrel.c")
    file(READ "${original}" code)
    tm_patch_sfx_defaults(squirrel code)
    tm_patch_fft_bindings(squirrel code)
    string(REPLACE "\"../build/assets/" "\"${TM_TIC80_SOURCE}/build/assets/" code "${code}")
    set(generated "${CMAKE_BINARY_DIR}/runtime_adapters/squirrel.c")
    tm_write_generated("${generated}" "${code}")
    get_target_property(sources squirrel SOURCES)
    list(REMOVE_ITEM sources "${original}")
    set_property(TARGET squirrel PROPERTY SOURCES "${sources}")
    target_sources(squirrel PRIVATE "${generated}")
endfunction()
