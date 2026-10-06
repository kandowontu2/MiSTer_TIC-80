# GCC 10 rejects repeated designators into tic_script's anonymous struct even
# when they follow declaration order. Keep this adaptation local to compilers
# that need it; constexpr retains static initialization and zeroed defaults.
function(tm_patch_miniscript_config variable)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
       NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
        return()
    endif()
    set(code "${${variable}}")
    set(original [=[extern "C" TIC_EXPORT const tic_script EXPORT_SCRIPT(MiniScript) =
{
    .id                     = 0x55,
    .name                   = "miniscript",
    .fileExtension          = ".ms",
    .projectComment         = "//",
    .init                 = initMiniScript,
    .close                = closeMiniScript,
    .tick                 = callMiniScriptTick,
    .boot                 = callMiniScriptBoot,

    .callback             =
    {
        .scanline           = tm_miniscript_scn,
        .border             = tm_miniscript_bdr,
        .menu               = tm_miniscript_menu,
    },

    .stdStringStartEnd      = "\"",
    .singleComment          = "//",

    .keywords               = MiniScriptKeywords,
    .keywordsCount          = COUNT_OF(MiniScriptKeywords),

    .demo = { DemoRom, sizeof(DemoRom) },
    .mark = { MarkRom, sizeof(MarkRom), "miniscriptmark.tic" },
};]=])
    string(FIND "${code}" "${original}" initializer_at)
    if(initializer_at EQUAL -1)
        message(FATAL_ERROR "Pinned MiniScript GCC 10 config initializer missing")
    endif()
    set(replacement [=[static constexpr tic_script tm_miniscript_config = []() constexpr {
    tic_script value{};
    value.id = 0x55;
    value.name = "miniscript";
    value.fileExtension = ".ms";
    value.projectComment = "//";
    value.init = initMiniScript;
    value.close = closeMiniScript;
    value.tick = callMiniScriptTick;
    value.boot = callMiniScriptBoot;
    value.callback.scanline = tm_miniscript_scn;
    value.callback.border = tm_miniscript_bdr;
    value.callback.menu = tm_miniscript_menu;
    value.stdStringStartEnd = "\"";
    value.singleComment = "//";
    value.keywords = MiniScriptKeywords;
    value.keywordsCount = COUNT_OF(MiniScriptKeywords);
    value.demo = {DemoRom, sizeof(DemoRom), nullptr};
    value.mark = {MarkRom, sizeof(MarkRom), "miniscriptmark.tic"};
    return value;
}();
extern "C" TIC_EXPORT const tic_script EXPORT_SCRIPT(MiniScript) = tm_miniscript_config;]=])
    string(REPLACE "${original}" "${replacement}" code "${code}")
    set(${variable} "${code}" PARENT_SCOPE)
endfunction()
