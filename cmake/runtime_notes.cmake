# Validate complete note names before publishing pitch outputs or calling SFX.
# Keep the reference checkout and its copyright notices untouched.
function(tm_note_replace variable old new)
    string(FIND "${${variable}}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Pinned note binding missing: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" changed "${${variable}}")
    set(${variable} "${changed}" PARENT_SCOPE)
endfunction()

function(tm_stage_runtime_notes)
    set(original_parser "${TM_TIC80_SOURCE}/src/api/parse_note.c")
    file(SHA256 "${original_parser}" parser_sha)
    if(NOT parser_sha STREQUAL "4e97d9ca3d33ad633f49b4cfbdb284914198463bded901559ca75baea28be02b")
        message(FATAL_ERROR "Pinned note parser repair source changed")
    endif()
    file(READ "${original_parser}" parser_code)
    tm_note_replace(parser_code "if(noteStr && strlen(noteStr) == 3)"
        "if(noteStr && strlen(noteStr) == 3 && noteStr[2] >= '0' && noteStr[2] <= '8')")
    tm_note_replace(parser_code "                break;" "                return true;")
    tm_note_replace(parser_code "\n        return true;\n" "\n")
    set(parser "${CMAKE_BINARY_DIR}/runtime_adapters/parse_note.c")
    tm_write_generated("${parser}" "${parser_code}")
    set(parser_targets 0)
    foreach(target luaapi js python wren squirrel miniscript ruby janet forth yuescript)
        if(TARGET ${target})
            get_target_property(sources ${target} SOURCES)
            if(original_parser IN_LIST sources)
                list(REMOVE_ITEM sources "${original_parser}")
                set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
                target_sources(${target} PRIVATE "${parser}")
                math(EXPR parser_targets "${parser_targets} + 1")
            endif()
        endif()
    endforeach()
    if((TM_BUILD_EXTENDED_RUNTIME AND NOT parser_targets EQUAL 10)
            OR (NOT TM_BUILD_EXTENDED_RUNTIME AND NOT parser_targets EQUAL 1))
        message(FATAL_ERROR "Pinned parser target coverage changed: ${parser_targets}")
    endif()
    if(NOT TM_BUILD_EXTENDED_RUNTIME)
        return()
    endif()

    set(original_js "${TM_TIC80_SOURCE}/src/api/js.c")
    file(SHA256 "${original_js}" js_sha)
    if(NOT js_sha STREQUAL "129bc80e3402020f4da61758215c32d92503b15f31e7aa92e290f406ff3ce520")
        message(FATAL_ERROR "Pinned JavaScript note error repair source changed")
    endif()
    file(READ "${original_js}" js_code)
    tm_note_replace(js_code "const char *noteStr = JS_ToCStringLen(ctx, NULL, argv[1]);"
        "size_t noteLength = 0;\n                    const char *noteStr = JS_ToCStringLen(ctx, &noteLength, argv[1]);")
    tm_note_replace(js_code [=[                    if(!parse_note(noteStr, &note, &octave))
                    {
                        throwError(ctx, "invalid note, should be like C#4");
                    }]=] [=[                    if(noteLength != 3 || !parse_note(noteStr, &note, &octave))
                    {
                        JS_FreeCString(ctx, noteStr);
                        throwError(ctx, "invalid note, should be like C#4");
                        return JS_EXCEPTION;
                    }]=])
    # QuickJS exception values must be returned to its caller. Setting the
    # pending exception and continuing can publish an invalid effect index.
    tm_note_replace(js_code "        throwError(ctx, \"unknown sfx index\");"
        "        throwError(ctx, \"unknown sfx index\");\n        return JS_EXCEPTION;")
    tm_note_replace(js_code "    else throwError(ctx, \"unknown channel\");"
        "    else\n    {\n        throwError(ctx, \"unknown channel\");\n        return JS_EXCEPTION;\n    }")
    tm_patch_sfx_defaults(js js_code)
    tm_patch_fft_bindings(js js_code)
    string(REPLACE "\"../build/assets/" "\"${TM_TIC80_SOURCE}/build/assets/" js_code "${js_code}")
    set(js_binding "${CMAKE_BINARY_DIR}/runtime_adapters/js.c")
    tm_write_generated("${js_binding}" "${js_code}")
    get_target_property(js_sources js SOURCES)
    list(REMOVE_ITEM js_sources "${original_js}")
    set_property(TARGET js PROPERTY SOURCES "${js_sources}")
    target_sources(js PRIVATE "${js_binding}")

    set(original_scheme "${TM_TIC80_SOURCE}/src/api/scheme.c")
    file(SHA256 "${original_scheme}" scheme_sha)
    if(NOT scheme_sha STREQUAL "e882e519cbaba685b9e1dc2b178a3885306c36736effdb3f92f40bca2c399e52")
        message(FATAL_ERROR "Pinned Scheme note parser repair source changed")
    endif()
    file(READ "${original_scheme}" scheme_code)
    # The omitted keyboard ID means any key. A byte containing -1 is 255,
    # which cannot match a pressed key and is outside the repeat-state array.
    string(REGEX MATCHALL "const tic_key code = argn > 0 \\? s7_integer\\(s7_car\\(args\\)\\) : -1" scheme_default_keys "${scheme_code}")
    list(LENGTH scheme_default_keys scheme_default_key_count)
    if(NOT scheme_default_key_count EQUAL 2)
        message(FATAL_ERROR "Pinned Scheme any-key bindings missing")
    endif()
    string(REPLACE "const tic_key code = argn > 0 ? s7_integer(s7_car(args)) : -1;"
        "const tic_key code = argn > 0 ? s7_integer(s7_car(args)) : tic_key_unknown;" scheme_code "${scheme_code}")
    tm_note_replace(scheme_code "#include <string.h>"
        "#include <string.h>\nextern bool parse_note(const char*, s32*, s32*);")
    tm_note_replace(scheme_code "    const s32 id = s7_integer(s7_car(args));\n\n    const int argn = s7_list_length(sc, args);\n    int note = -1;"
        "    const s32 id = s7_integer(s7_car(args));\n    if (id >= SFX_COUNT)\n        return s7_error(sc, s7_make_symbol(sc, \"invalid-sfx-index\"),\n            s7_list(sc, 1, s7_make_string(sc, \"invalid sfx index\")));\n\n    const int argn = s7_list_length(sc, args);\n    int note = -1;")
    tm_note_replace(scheme_code "    const s32 channel = argn > 3 ? s7_integer(s7_cadddr(args)) : 0;\n\n    s32 volumes[TIC80_SAMPLE_CHANNELS]"
        "    const s32 channel = argn > 3 ? s7_integer(s7_cadddr(args)) : 0;\n    if (channel < 0 || channel >= TIC_SOUND_CHANNELS)\n        return s7_error(sc, s7_make_symbol(sc, \"invalid-channel\"),\n            s7_list(sc, 1, s7_make_string(sc, \"invalid channel\")));\n\n    s32 volumes[TIC80_SAMPLE_CHANNELS]")
    tm_note_replace(scheme_code [=[            const u8 len = s7_string_length(note_ptr);
            if (len == 3) {
                const u8 modif = get_note_modif(note_str[1]);
                note = get_note_base(note_str[0]);
                octave = get_note_octave(note_str[2]);
                if (note < 255 || modif < 255 || octave < 255) {
                    note = note + modif;
                } else {
                    note = octave = 255;
                }
            }
            /* if (note == 255 || octave == 255) { */
            /*     char buffer[100]; */
            /*     snprintf(buffer, 99, "Invalid sfx note given: %s\n", note_str); */
            /*     tic->data->error(tic->data->data, buffer); */
            /* } */]=] [=[            if (s7_string_length(note_ptr) != 3 || !parse_note(note_str, &note, &octave)) {
                return s7_error(sc, s7_make_symbol(sc, "invalid-note"),
                    s7_list(sc, 1, s7_make_string(sc, "invalid note, should be like C#4")));
            }]=])
    tm_patch_sfx_defaults(scheme scheme_code)
    string(REPLACE "\"../build/assets/" "\"${TM_TIC80_SOURCE}/build/assets/" scheme_code "${scheme_code}")
    set(scheme_binding "${CMAKE_BINARY_DIR}/runtime_adapters/scheme.c")
    tm_write_generated("${scheme_binding}" "${scheme_code}")
    get_target_property(scheme_sources scheme SOURCES)
    list(REMOVE_ITEM scheme_sources "${original_scheme}")
    set_property(TARGET scheme PROPERTY SOURCES "${scheme_sources}")
    target_sources(scheme PRIVATE "${scheme_binding}" "${parser}")
endfunction()
