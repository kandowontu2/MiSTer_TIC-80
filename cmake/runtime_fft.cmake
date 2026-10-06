# Add missing FFT/VQT bindings in staged sources, preserving the pinned checkout.
function(tm_fft_replace variable before after)
    set(code "${${variable}}")
    string(FIND "${code}" "${before}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Pinned FFT binding insertion span missing")
    endif()
    string(REPLACE "${before}" "${after}" code "${code}")
    set(${variable} "${code}" PARENT_SCOPE)
endfunction()

# Limit repairs to one named spectrum function, retaining other API coercions.
function(tm_fft_function_replace variable marker before after)
    set(code "${${variable}}")
    string(FIND "${code}" "${marker}" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "Pinned spectrum function missing: ${marker}")
    endif()
    string(SUBSTRING "${code}" ${start} -1 tail)
    string(FIND "${tail}" "\nstatic " end)
    if(end EQUAL -1)
        message(FATAL_ERROR "Pinned spectrum function boundary missing: ${marker}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} previous)
    set(updated "${previous}")
    tm_fft_replace(updated "${before}" "${after}")
    string(REPLACE "${previous}" "${updated}" code "${code}")
    set(${variable} "${code}" PARENT_SCOPE)
endfunction()

function(tm_patch_fft_bindings language variable)
    set(code "${${variable}}")
    if(language STREQUAL "luaapi")
        tm_fft_replace(code "#include <ctype.h>" "#include <ctype.h>\n#include <math.h>")
        set(helper [=[static s32 tm_lua_spectrum_integer(lua_State* lua, s32 index)
{
    /* Retain Lua's tonumber coercion and truncation of finite fractions. */
    double number = trunc(lua_tonumber(lua, index));
    if (!isfinite(number) || number < INT32_MIN || number > INT32_MAX)
        return luaL_error(lua, "invalid spectrum integer");
    return (s32)number;
}

]=])
        tm_fft_replace(code "static s32 lua_vqt(" "${helper}static s32 lua_vqt(")
        foreach(api IN ITEMS vqt vqts vqtr vqtrs vqtw vqtsw vqtrw vqtrsw fft ffts fftr fftrs)
            tm_fft_function_replace(code "static s32 lua_${api}(" "getLuaNumber(lua, 1)" "tm_lua_spectrum_integer(lua, 1)")
            if(api MATCHES "^fft")
                tm_fft_function_replace(code "static s32 lua_${api}(" "getLuaNumber(lua, 2)" "tm_lua_spectrum_integer(lua, 2)")
            endif()
        endforeach()
    elseif(language STREQUAL "wren")
        file(SHA256 "${TM_TIC80_SOURCE}/src/api/wren.c" original_sha)
        if(NOT original_sha STREQUAL "f243df9c3aaa64247e181c24ceb3cc661b55fe7224caf2d41f738161fa599652")
            message(FATAL_ERROR "Pinned Wren spectrum source changed")
        endif()
        tm_fft_replace(code "#include <ctype.h>" "#include <ctype.h>\n#include <math.h>")
        set(helper [=[static bool tm_wren_spectrum_integer(WrenVM* vm, s32 index, s32* output)
{
    if (wrenGetSlotType(vm, index) != WREN_TYPE_NUM) {
        wrenError(vm, "invalid spectrum integer");
        return false;
    }
    double number = trunc(wrenGetSlotDouble(vm, index));
    if (!isfinite(number) || number < INT32_MIN || number > INT32_MAX) {
        wrenError(vm, "invalid spectrum integer");
        return false;
    }
    *output = (s32)number;
    return true;
}

]=])
        tm_fft_replace(code "static void wren_vqt(" "${helper}static void wren_vqt(")
        foreach(api IN ITEMS vqt vqts vqtr vqtrs vqtw vqtsw vqtrw vqtrsw)
            tm_fft_function_replace(code "static void wren_${api}(" "double bin = getWrenNumber(vm, 1);" "s32 bin;\n        if (!tm_wren_spectrum_integer(vm, 1, &bin)) return;")
        endforeach()
        foreach(api IN ITEMS fft ffts fftr fftrs)
            tm_fft_function_replace(code "static void wren_${api}(" "double start_freq = getWrenNumber(vm, 1);" "s32 start_freq;\n        if (!tm_wren_spectrum_integer(vm, 1, &start_freq)) return;")
            tm_fft_function_replace(code "static void wren_${api}(" "double end_freq = -1;" "s32 end_freq = -1;")
            tm_fft_function_replace(code "static void wren_${api}(" "end_freq = getWrenNumber(vm, 2);" "if (!tm_wren_spectrum_integer(vm, 2, &end_freq)) return;")
        endforeach()
    elseif(language STREQUAL "squirrel")
        file(SHA256 "${TM_TIC80_SOURCE}/src/api/squirrel.c" original_sha)
        if(NOT original_sha STREQUAL "3bba3f35bc3818e8093324abdb10d9abc2c66f79f774e05732d19b646ef1876a")
            message(FATAL_ERROR "Pinned Squirrel spectrum source changed")
        endif()
        tm_fft_replace(code "#include <ctype.h>" "#include <ctype.h>\n#include <math.h>")
        set(helper [=[static bool tm_squirrel_spectrum_integer(HSQUIRRELVM vm, s32 index, s32* output)
{
    if (sq_gettype(vm, index) == OT_FLOAT) {
        /* sq_getinteger casts floats before validating their range. */
        SQFloat value;
        if (SQ_FAILED(sq_getfloat(vm, index, &value))) return false;
        double number = trunc((double)value);
        if (!isfinite(number) || number < INT32_MIN || number > INT32_MAX) {
            sq_throwerror(vm, "invalid spectrum integer");
            return false;
        }
        *output = (s32)number;
    } else {
        /* Retain integer narrowing, bool values and nonnumeric zero fallback. */
        *output = getSquirrelNumber(vm, index);
    }
    return true;
}

]=])
        tm_fft_replace(code "static SQInteger squirrel_vqt(" "${helper}static SQInteger squirrel_vqt(")
        foreach(api IN ITEMS vqt vqts vqtr vqtrs vqtw vqtsw vqtrw vqtrsw)
            tm_fft_function_replace(code "static SQInteger squirrel_${api}(" "double bin = getSquirrelNumber(vm, 2);" "s32 bin;\n        if (!tm_squirrel_spectrum_integer(vm, 2, &bin)) return SQ_ERROR;")
        endforeach()
        foreach(api IN ITEMS fft ffts fftr fftrs)
            tm_fft_function_replace(code "static SQInteger squirrel_${api}(" "double start_freq = getSquirrelNumber(vm, 2);" "s32 start_freq;\n        if (!tm_squirrel_spectrum_integer(vm, 2, &start_freq)) return SQ_ERROR;")
            tm_fft_function_replace(code "static SQInteger squirrel_${api}(" "double end_freq = -1;" "s32 end_freq = -1;")
            tm_fft_function_replace(code "static SQInteger squirrel_${api}(" "end_freq = getSquirrelNumber(vm, 3);" "if (!tm_squirrel_spectrum_integer(vm, 3, &end_freq)) return SQ_ERROR;")
        endforeach()
    elseif(language STREQUAL "js")
        file(SHA256 "${TM_TIC80_SOURCE}/src/api/js.c" original_sha)
        if(NOT original_sha STREQUAL "129bc80e3402020f4da61758215c32d92503b15f31e7aa92e290f406ff3ce520")
            message(FATAL_ERROR "Pinned JavaScript spectrum source changed")
        endif()
        foreach(api IN ITEMS vqt vqts vqtr vqtrs vqtw vqtsw vqtrw vqtrsw)
            tm_fft_function_replace(code "static JSValue js_${api}(" "s32 bin = getInteger(ctx, argv[0]);" "s32 bin;\n    if (JS_ToInt32(ctx, &bin, argv[0])) return JS_EXCEPTION;")
        endforeach()
        foreach(api IN ITEMS fft ffts fftr fftrs)
            tm_fft_function_replace(code "static JSValue js_${api}(" "s32 start_freq = getInteger(ctx, argv[0]);" "s32 start_freq;\n    if (JS_ToInt32(ctx, &start_freq, argv[0])) return JS_EXCEPTION;")
            tm_fft_function_replace(code "static JSValue js_${api}(" "s32 end_freq = getInteger2(ctx, argv[1], -1);" "s32 end_freq = -1;\n    if (!JS_IsUndefined(argv[1]) && JS_ToInt32(ctx, &end_freq, argv[1])) return JS_EXCEPTION;")
        endforeach()
    elseif(language STREQUAL "wasm")
        file(SHA256 "${TM_TIC80_SOURCE}/src/api/wasm.c" original_sha)
        if(NOT original_sha STREQUAL "a7970b9a91fb9e6ff9c742932977661d29e3d441f7684877df534097cb8124f1")
            message(FATAL_ERROR "Pinned WASM FFT source changed")
        endif()
        set(functions "")
        set(imports "")
        foreach(api IN ITEMS fft ffts fftr fftrs)
            string(APPEND functions "m3ApiRawFunction(wasmtic_${api})\n{\n    m3ApiReturnType(double)\n    m3ApiGetArg(int32_t, startFreq);\n    m3ApiGetArg(int32_t, endFreq);\n    tic_core* core = getWasmCore(runtime);\n    m3ApiReturn(core->api.${api}((tic_mem*)core, startFreq, endFreq));\n}\n\n")
            # wasm3 spells f64 as F. Every import has a complete numeric ABI.
            string(APPEND imports "    _ (SuppressLookupFailure(m3_LinkRawFunction(module, \"env\", \"${api}\", \"F(ii)\", &wasmtic_${api})));\n")
        endforeach()
        foreach(api IN ITEMS vqt vqts vqtr vqtrs vqtw vqtsw vqtrw vqtrsw)
            string(APPEND functions "m3ApiRawFunction(wasmtic_${api})\n{\n    m3ApiReturnType(double)\n    m3ApiGetArg(int32_t, bin);\n    tic_core* core = getWasmCore(runtime);\n    m3ApiReturn(core->api.${api}((tic_mem*)core, bin));\n}\n\n")
            string(APPEND imports "    _ (SuppressLookupFailure(m3_LinkRawFunction(module, \"env\", \"${api}\", \"F(i)\", &wasmtic_${api})));\n")
        endforeach()
        tm_fft_replace(code "m3ApiRawFunction(wasmtic_time)" "${functions}m3ApiRawFunction(wasmtic_time)")
        set(link_start "M3Result linkTicAPI(IM3Module module)\n{\n    M3Result result = m3Err_none;\n")
        tm_fft_replace(code "${link_start}" "${link_start}${imports}")
    elseif(language STREQUAL "miniscript")
        file(SHA256 "${TM_TIC80_SOURCE}/src/api/miniscript.cpp" original_sha)
        if(NOT original_sha STREQUAL "8c86a48bb4de693232fee0fed0304b8497e37546b59b32b8a6e931804981e487")
            message(FATAL_ERROR "Pinned MiniScript FFT source changed")
        endif()
        tm_fft_replace(code "#include \"miniscript.h\"" "#include \"miniscript.h\"\n#include <cmath>\n#include <cstdint>")
        set(integer_helper [=[static bool tm_fft_integer(Value value, s32* output) {
    if (!value.IsNumber()) return false;
    double number = value.DoubleValue();
    if (!std::isfinite(number) || number < INT32_MIN || number > INT32_MAX) return false;
    *output = (s32)number;
    return true;
}

]=])
        tm_fft_replace(code "static void TIC80Intrinsics(ValueDict& tic80Module) {" "${integer_helper}static void TIC80Intrinsics(ValueDict& tic80Module) {")
        set(functions "")
        foreach(api IN ITEMS fft ffts fftr fftrs)
            string(APPEND functions "\n    // ${api}(start_freq, end_freq=-1)\n    i = Intrinsic::Create(\"\");\n    i.AddParam(\"start_freq\");\n    i.AddParam(\"end_freq\", -1);\n    i.set_Code(INTRINSIC_LAMBDA {\n        tic_core* core = getCore(context);\n        s32 start, end = -1;\n        Value last = context.GetArg(1);\n        if (!tm_fft_integer(context.GetArg(0), &start) ||\n            (!last.IsNull() && !tm_fft_integer(last, &end))) {\n            core->data->error(core->data->data, \"invalid ${api} frequency\");\n            return IntrinsicResult::Null;\n        }\n        return IntrinsicResult(core->api.${api}((tic_mem*)core, start, end));\n    });\n    tic80Module.SetValue(\"${api}\", i.GetFunc());\n")
        endforeach()
        foreach(api IN ITEMS vqt vqts vqtr vqtrs vqtw vqtsw vqtrw vqtrsw)
            string(APPEND functions "\n    // ${api}(bin)\n    i = Intrinsic::Create(\"\");\n    i.AddParam(\"bin\");\n    i.set_Code(INTRINSIC_LAMBDA {\n        tic_core* core = getCore(context);\n        s32 bin;\n        if (!tm_fft_integer(context.GetArg(0), &bin)) {\n            core->data->error(core->data->data, \"invalid ${api} bin\");\n            return IntrinsicResult::Null;\n        }\n        return IntrinsicResult(core->api.${api}((tic_mem*)core, bin));\n    });\n    tic80Module.SetValue(\"${api}\", i.GetFunc());\n")
        endforeach()
        set(module_end "    tic80Module.SetValue(\"vbank\", i.GetFunc());\n}")
        tm_fft_replace(code "${module_end}" "    tic80Module.SetValue(\"vbank\", i.GetFunc());\n${functions}}")
    elseif(language STREQUAL "forth")
        file(SHA256 "${TM_TIC80_SOURCE}/src/api/forth.c" original_sha)
        if(NOT original_sha STREQUAL "e94af80e9cc7d3c0f89dbfbd6256f6e14b3c398e9ca647b81d20deeacefa6e41")
            message(FATAL_ERROR "Pinned Forth FFT source changed")
        endif()
        set(functions "")
        foreach(api IN ITEMS fftr fftrs)
            string(APPEND functions "static cell_t tic_forth_${api}(void)\n{\n    s32 end = (s32)pfPopFromStack();\n    s32 start = (s32)pfPopFromStack();\n    double value = gForthCore->api.${api}((tic_mem*)gForthCore, start, end);\n    /* Raw magnitudes need not fit the normalized FFT range. */\n    if (value != value) return 0;\n    if (value >= INT32_MAX / 65535.0) return (cell_t)INT32_MAX;\n    if (value <= INT32_MIN / 65535.0) return (cell_t)INT32_MIN;\n    return (cell_t)(s32)(value * 65535.0);\n}\n\n")
        endforeach()
        set(vqt_table "")
        set(vqt_words "")
        foreach(api IN ITEMS vqt vqts vqtr vqtrs vqtw vqtsw vqtrw vqtrsw)
            string(TOUPPER "${api}" word)
            string(APPEND functions "static cell_t tic_forth_${api}(void)\n{\n    s32 bin = (s32)pfPopFromStack();\n    double value = gForthCore->api.${api}((tic_mem*)gForthCore, bin);\n    /* Preserve the Forth spectrum scale without overflowing a signed cell. */\n    if (value != value) return 0;\n    if (value >= INT32_MAX / 65535.0) return (cell_t)INT32_MAX;\n    if (value <= INT32_MIN / 65535.0) return (cell_t)INT32_MIN;\n    return (cell_t)(s32)(value * 65535.0);\n}\n\n")
            string(APPEND vqt_table "    (CFunc0)tic_forth_${api},\n")
            string(APPEND vqt_words "    if (CreateGlueToC(\"${word}\", i++, C_RETURNS_VALUE, 0) < 0) return -1;\n")
        endforeach()
        tm_fft_replace(code "CFunc0 CustomFunctionTable[] =" "${functions}CFunc0 CustomFunctionTable[] =")
        set(table_end "    (CFunc0)tic_forth_ffts,         // 51  FFTS\n};")
        tm_fft_replace(code "${table_end}" "    (CFunc0)tic_forth_ffts,         // 51  FFTS\n    (CFunc0)tic_forth_fftr,         // 52  FFTR\n    (CFunc0)tic_forth_fftrs,        // 53  FFTRS\n${vqt_table}};")
        set(words_end "    if (CreateGlueToC(\"FFTS\",   i++, C_RETURNS_VALUE, 0) < 0) return -1;\n    return 0;")
        tm_fft_replace(code "${words_end}" "    if (CreateGlueToC(\"FFTS\",   i++, C_RETURNS_VALUE, 0) < 0) return -1;\n    if (CreateGlueToC(\"FFTR\",   i++, C_RETURNS_VALUE, 0) < 0) return -1;\n    if (CreateGlueToC(\"FFTRS\",  i++, C_RETURNS_VALUE, 0) < 0) return -1;\n${vqt_words}    return 0;")
    endif()
    set(${variable} "${code}" PARENT_SCOPE)
endfunction()
