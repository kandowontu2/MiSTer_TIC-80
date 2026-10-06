# Build the actual pinned console/editors without SDL or a desktop window.
function(tm_add_studio)
    configure_file("${TM_PROJECT_SOURCE}/assets/cacert.pem" "${CMAKE_BINARY_DIR}/cacert.pem" COPYONLY)
    set(CMAKE_SOURCE_DIR "${TM_TIC80_SOURCE}")
    set(THIRDPARTY_DIR "${TM_TIC80_SOURCE}/vendor")
    set(BUILD_EDITORS ON)
    set(BUILD_SURF ON)
    set(BUILD_PRO ON)
    set(BUILD_SDLGPU OFF)
    set(USE_NAETT OFF)
    set(PREFER_SYSTEM_LIBRARIES OFF)
    include("${TM_TIC80_SOURCE}/cmake/runtime_versions.cmake")
    # The upstream zip recipe assumes a top-level source tree and changes the
    # global testing cache. This parent needs only its library, not its tests.
    add_library(zip STATIC "${THIRDPARTY_DIR}/zip/src/zip.c")
    target_include_directories(zip PUBLIC "${THIRDPARTY_DIR}/zip/src")
    include("${TM_TIC80_SOURCE}/cmake/argparse.cmake")
    include("${TM_TIC80_SOURCE}/cmake/wave.cmake")
    include("${TM_TIC80_SOURCE}/cmake/studio.cmake")
    # Keep the pinned checkout intact. Expose the existing popup for platform
    # messages and guard the editor's FFT reset when capture is unavailable.
    set(original_studio "${TM_TIC80_SOURCE}/src/studio/studio.c")
    file(READ "${original_studio}" studio_code)
    file(SHA256 "${original_studio}" studio_digest)
    if(NOT studio_digest STREQUAL "0a20b583fe3dd2969cf92fb14ccd1bf08ff6e09b737679ece8b4c03af0b61d54")
        message(FATAL_ERROR "Pinned Studio shortcut implementation changed")
    endif()
    # A code popup owns Escape before the underlying Vi command does. Keep
    # the upstream Vi dispatch only for the ordinary text-edit view.
    set(vi_escape [=[                && getStudioViMode(studio) != VI_NORMAL]=])
    string(FIND "${studio_code}" "${vi_escape}" vi_escape_at)
    if(vi_escape_at LESS 0)
        message(FATAL_ERROR "Pinned Studio Vi Escape routing changed")
    endif()
    string(REPLACE "${vi_escape}" [=[                && getStudioViMode(studio) != VI_NORMAL
                && studio->mode == TIC_CODE_MODE
                && studio->code->mode == TEXT_EDIT_MODE]=] studio_code "${studio_code}")
    if(NOT studio_code MATCHES "static void showPopupMessage\\(Studio\\* studio, const char\\* text\\)")
        message(FATAL_ERROR "Pinned Studio popup definition changed")
    endif()
    string(REPLACE "showPopupMessage(" "tm_studio_popup(" studio_code "${studio_code}")
    string(REPLACE "static void tm_studio_popup(" "void tm_studio_popup(" studio_code "${studio_code}")
    string(REPLACE "    bool alive;" "    bool alive;\n    bool tm_hash_loaded;\n    void* tm_confirm_data;" studio_code "${studio_code}")
    # Upstream ESC/editor shortcuts and direct teardown can leave a confirmation
    # without invoking either answer. Track its owned callback context so the
    # supervisor can resolve NO and release it exactly once.
    set(confirm_owner "NULL, MOVE((ConfirmData){studio, callback, data}));")
    string(FIND "${studio_code}" "${confirm_owner}" confirm_at)
    if(confirm_at LESS 0)
        message(FATAL_ERROR "Pinned Studio confirmation ownership changed")
    endif()
    string(REPLACE "${confirm_owner}" "NULL, studio->tm_confirm_data = MOVE((ConfirmData){studio, callback, data}));" studio_code "${studio_code}")
    string(REPLACE "Studio* studio = confirmData->studio;" "Studio* studio = confirmData->studio;\n        if(studio->tm_confirm_data == confirmData) studio->tm_confirm_data = NULL;" studio_code "${studio_code}")
    string(REPLACE "void confirmDialog(Studio* studio, const char** text, s32 rows, ConfirmCallback callback, void* data)\n{"
        "void tm_studio_cancel_confirmation(Studio* studio)\n{\n    if(studio->tm_confirm_data) confirmHandler(false, studio->tm_confirm_data);\n}\n\nvoid confirmDialog(Studio* studio, const char** text, s32 rows, ConfirmCallback callback, void* data)\n{\n    tm_studio_cancel_confirmation(studio);"
        studio_code "${studio_code}")
    string(REPLACE "bool studio_alive(Studio* studio)" "bool tm_studio_hashload_succeeded(Studio* studio) { return studio->tm_hash_loaded; }\nvoid tm_studio_hashload_result(Studio* studio, bool success) { studio->tm_hash_loaded = success; }\n\nbool studio_alive(Studio* studio)" studio_code "${studio_code}")
    string(REPLACE "bool studio_alive(Studio* studio)" "void tm_studio_saved_hash(Studio* studio, u8 out[16]) { memcpy(out, studio->cart.hash.data, 16); }\nvoid tm_studio_restore_saved_hash(Studio* studio, const u8 hash[16]) { memcpy(studio->cart.hash.data, hash, 16); }\n\nbool studio_alive(Studio* studio)" studio_code "${studio_code}")
    string(PREPEND studio_code "#include \"tic80_mister/studio.h\"\n")
    string(PREPEND studio_code "#include \"tic80_mister/history_private.h\"\n")
    set(run_mode "case TIC_RUN_MODE:      initRunMode(studio); break;")
    string(FIND "${studio_code}" "${run_mode}" run_at)
    if(run_at LESS 0)
        message(FATAL_ERROR "Pinned Studio RUN transition changed")
    endif()
    string(REPLACE "${run_mode}" "case TIC_RUN_MODE:      initRunMode(studio); if(tm_studio_pmem_failed()) mode = TIC_CONSOLE_MODE; break;" studio_code "${studio_code}")
    set(bank_checkpoint [=[
void tm_studio_bank_state(Studio* studio, tm_studio_banks* out)
{
    _Static_assert(sizeof studio->bank.indexes == sizeof out->indexes, "Pinned Studio bank modes changed");
    memcpy(out->indexes, studio->bank.indexes, sizeof out->indexes);
    out->show = studio->bank.show;
    out->chained = studio->bank.chained;
}
bool tm_studio_restore_banks(Studio* studio, const tm_studio_banks* banks)
{
    for(unsigned i = 0; i < sizeof banks->indexes; ++i)
        if(banks->indexes[i] >= TIC_EDITOR_BANKS) return false;
    memcpy(studio->bank.indexes, banks->indexes, sizeof banks->indexes);
    studio->bank.show = banks->show;
    studio->bank.chained = banks->chained;
    return true;
}
void tm_studio_sprite_views(Studio* studio, tm_studio_sprite_view out[8])
{
    _Static_assert(TIC_EDITOR_BANKS == 8, "Pinned sprite editor bank count changed");
    for(unsigned i = 0; i < TIC_EDITOR_BANKS; ++i)
    {
        Sprite* sprite = studio->banks.sprite[i];
        out[i] = (tm_studio_sprite_view){
            .x = sprite->x, .y = sprite->y, .size = sprite->size,
            .brush_size = sprite->brushSize, .color = sprite->color, .color2 = sprite->color2,
            .bpp = sprite->blit.mode, .bank = sprite->blit.bank, .page = sprite->blit.page,
            .tool = sprite->mode,
            .flags = (sprite->advanced ? TM_SPRITE_ADVANCED : 0) |
                (sprite->hexindex ? TM_SPRITE_HEX_INDEX : 0) |
                (sprite->palette.vbank1 ? TM_SPRITE_PALETTE_BANK1 : 0),
        };
    }
}
bool tm_studio_restore_sprite_views(Studio* studio, const tm_studio_sprite_view views[8])
{
    for(unsigned i = 0; i < TIC_EDITOR_BANKS; ++i)
        if(!tm_studio_sprite_view_valid(&views[i])) return false;
    for(unsigned i = 0; i < TIC_EDITOR_BANKS; ++i)
    {
        Sprite* sprite = studio->banks.sprite[i];
        const tm_studio_sprite_view* view = &views[i];
        sprite->x = view->x; sprite->y = view->y; sprite->size = view->size;
        sprite->brushSize = view->brush_size; sprite->color = view->color; sprite->color2 = view->color2;
        sprite->mode = view->tool;
        sprite->advanced = (view->flags & TM_SPRITE_ADVANCED) != 0;
        sprite->hexindex = (view->flags & TM_SPRITE_HEX_INDEX) != 0;
        sprite->palette.vbank1 = (view->flags & TM_SPRITE_PALETTE_BANK1) != 0;
        sprite->blit.bank = view->bank; sprite->blit.page = view->page;
        tic_blit_update_bpp(&sprite->blit, view->bpp);
        sprite->index = sprite->y * sprite->blit.pages * TIC_SPRITESHEET_COLS + sprite->x;
        sprite->sheet = tic_tilesheet_get(tic_blit_calc_segment(&sprite->blit), (u8*)sprite->src);
    }
    return true;
}
bool tm_code_restore_view_mode(Code*, const tm_studio_code_view*);
void tm_studio_code_state(Studio* studio, tm_studio_code_view* out)
{
    Code* code = studio->code;
    bool modal = code->mode != TEXT_EDIT_MODE && code->mode != TEXT_DRAG_CODE;
    bool sidebar = code->mode == TEXT_BOOKMARK_MODE || code->mode == TEXT_OUTLINE_MODE;
    *out = (tm_studio_code_view){
        .cursor = code->cursor.position - code->src,
        .selection = code->cursor.selection ? code->cursor.selection - code->src : -1,
        .column = code->cursor.column, .scroll_x = code->scroll.x, .scroll_y = code->scroll.y,
        .flags = (code->altFont ? TM_CODE_ALT_FONT : 0) | (code->shadowText ? TM_CODE_SHADOW : 0),
        .vi_mode = getStudioViMode(studio),
        .mode = code->mode,
        .animation = code->anim.movie == &code->anim.show ? TM_CODE_SHOW : code->anim.movie == &code->anim.hide ? TM_CODE_HIDE : TM_CODE_IDLE,
        .animation_tick = code->anim.movie == &code->anim.idle ? 0 : code->anim.movie->tick,
        .popup_y = code->anim.pos, .sidebar_x = code->anim.sidebar,
        .previous_cursor = modal && code->popup.prevPos ? code->popup.prevPos - code->src : -1,
        .previous_selection = modal && code->popup.prevSel ? code->popup.prevSel - code->src : -1,
        .replace_offset = code->mode == TEXT_REPLACE_MODE && code->popup.offset ? code->popup.offset - code->popup.text : -1,
        .jump_line = code->mode == TEXT_GOTO_MODE ? code->jump.line : -1,
        .sidebar_count = sidebar ? code->sidebar.size : 0,
        .sidebar_index = sidebar ? code->sidebar.index : 0,
        .sidebar_scroll = sidebar ? code->sidebar.scroll : 0,
    };
    _Static_assert(sizeof code->popup.text == TM_CODE_POPUP_BYTES, "Pinned code popup size changed");
    memcpy(out->popup_text, code->popup.text, sizeof code->popup.text);
}
bool tm_studio_restore_code(Studio* studio, const tm_studio_code_view* view)
{
    Code* code = studio->code;
    if(!tm_studio_code_view_valid(view, strnlen(code->src, TIC_CODE_SIZE))) return false;
    if(!tm_code_restore_view_mode(code, view)) return false;
    code->cursor.position = code->src + view->cursor;
    code->cursor.selection = view->selection < 0 ? NULL : code->src + view->selection;
    code->altFont = (view->flags & TM_CODE_ALT_FONT) != 0;
    code->shadowText = (view->flags & TM_CODE_SHADOW) != 0;
    setStudioViMode(studio, view->vi_mode);
    // Rebuild syntax, delimiter and status state from the restored cartridge.
    // update chooses the actual column; restore the preferred vertical column
    // and manual scroll afterwards. Active mouse gestures remain released.
    code->update(code);
    code->cursor.column = view->column;
    code->scroll.x = view->scroll_x; code->scroll.y = view->scroll_y;
    return true;
}
uint64_t tm_studio_code_history_revision(Studio* studio)
{ return tm_history_revision(studio->code->history); }
int tm_studio_code_history_snapshot(Studio* studio, u32 request)
{ return tm_history_snapshot(studio->code->history, request); }
bool tm_studio_code_history_restore(Studio* studio, int descriptor)
{
    Code* code = studio->code;
    _Static_assert(sizeof *code->state == 2, "Pinned code history state changed");
    History* restored = tm_history_snapshot_restore(code->state, sizeof *code->state * TIC_CODE_SIZE, descriptor);
    if(!restored) return false;
    history_delete(code->history); code->history = restored;
    return true;
}
static void tm_history_slots(Studio* studio, History** slots[TM_STUDIO_HISTORY_COUNT])
{
    _Static_assert(TIC_EDITOR_BANKS == 8 && TM_STUDIO_HISTORY_COUNT == 1 + 5 * TIC_EDITOR_BANKS, "Pinned editor history count changed");
    slots[0] = &studio->code->history;
    for(unsigned i = 0; i < TIC_EDITOR_BANKS; ++i)
    {
        slots[1 + 5*i] = &studio->banks.sprite[i]->history;
        slots[2 + 5*i] = &studio->banks.map[i]->history;
        slots[3 + 5*i] = &studio->banks.sfx[i]->history;
        slots[4 + 5*i] = &studio->banks.sfx[i]->waveHistory;
        slots[5 + 5*i] = &studio->banks.music[i]->history;
    }
}
uint64_t tm_studio_history_revision(Studio* studio)
{
    History** slots[TM_STUDIO_HISTORY_COUNT]; tm_history_slots(studio, slots);
    uint64_t revision = 0;
    for(unsigned i = 0; i < TM_STUDIO_HISTORY_COUNT; ++i)
    {
        uint64_t current = tm_history_revision(*slots[i]);
        if(current > revision) revision = current;
    }
    return revision;
}
int tm_studio_history_snapshot(Studio* studio, u32 request)
{
    History** slots[TM_STUDIO_HISTORY_COUNT]; tm_history_slots(studio, slots);
    History* histories[TM_STUDIO_HISTORY_COUNT];
    for(unsigned i = 0; i < TM_STUDIO_HISTORY_COUNT; ++i) histories[i] = *slots[i];
    return tm_history_bundle_snapshot(histories, TM_STUDIO_HISTORY_COUNT, request);
}
bool tm_studio_history_snapshot_valid(int descriptor, u32* request)
{
    u32 sizes[TM_STUDIO_HISTORY_COUNT] = {2 * TIC_CODE_SIZE};
    for(unsigned i = 0; i < TIC_EDITOR_BANKS; ++i)
    {
        sizes[1 + 5*i] = TIC_SPRITES * sizeof(tic_tile);
        sizes[2 + 5*i] = sizeof(tic_map);
        sizes[3 + 5*i] = sizeof(tic_samples);
        sizes[4 + 5*i] = sizeof(tic_waveforms);
        sizes[5 + 5*i] = sizeof(tic_music);
    }
    return tm_history_bundle_valid(descriptor, sizes, TM_STUDIO_HISTORY_COUNT, request);
}
bool tm_studio_history_restore(Studio* studio, int descriptor)
{
    History** slots[TM_STUDIO_HISTORY_COUNT]; tm_history_slots(studio, slots);
    const bool restore_current[TM_STUDIO_HISTORY_COUNT] = {true};
    return tm_history_bundle_restore(slots, restore_current, TM_STUDIO_HISTORY_COUNT, descriptor);
}

]=])
    string(REPLACE "bool studio_alive(Studio* studio)" "${bank_checkpoint}bool studio_alive(Studio* studio)" studio_code "${studio_code}")
    set(close_net "#if defined(BUILD_EDITORS) || defined(BUILD_SURF)\n    tic_net_close(studio->net);\n#endif")
    string(REPLACE "${close_net}" "" studio_code "${studio_code}")
    string(REPLACE "void studio_delete(Studio* studio)\n{" "void studio_delete(Studio* studio)\n{\n    tm_studio_cancel_confirmation(studio);\n${close_net}" studio_code "${studio_code}")
    set(fft_begin "    if (studio->config->data.fft) {\n        // initialize FFT data structures")
    set(fft_end "        memset(fftNormalizedMaxData, 0, sizeof(fftNormalizedMaxData[0]) * FFT_SIZE);\n    }")
    string(FIND "${studio_code}" "${fft_begin}" fft_begin_at)
    string(FIND "${studio_code}" "${fft_end}" fft_end_at)
    if(fft_begin_at LESS 0 OR fft_end_at LESS 0)
        message(FATAL_ERROR "Pinned Studio FFT initialization changed")
    endif()
    string(REPLACE "${fft_begin}" "#ifndef TIC80_FFT_UNSUPPORTED\n${fft_begin}" studio_code "${studio_code}")
    string(REPLACE "${fft_end}" "${fft_end}\n#endif" studio_code "${studio_code}")
    set(staged_studio "${CMAKE_BINARY_DIR}/studio_adapters/studio.c")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/studio_adapters")
    tm_write_generated("${staged_studio}" "${studio_code}")
    get_target_property(studio_sources tic80studio SOURCES)
    list(REMOVE_ITEM studio_sources "${original_studio}")
    list(REMOVE_ITEM studio_sources "${TM_TIC80_SOURCE}/src/studio/net.c")
    list(REMOVE_ITEM studio_sources "${TM_TIC80_SOURCE}/src/ext/history.c")
    set_property(TARGET tic80studio PROPERTY SOURCES "${studio_sources}")
    target_sources(tic80studio PRIVATE "${staged_studio}" "${TM_PROJECT_SOURCE}/src/studio_net.c")
    target_sources(tic80studio PRIVATE "${TM_PROJECT_SOURCE}/src/studio_rom.c")
    # Preserve the pinned history operations while exposing an O(1) change
    # revision and a common layout for pointer-free sealed snapshots.
    set(original_history "${TM_TIC80_SOURCE}/src/ext/history.c")
    file(SHA256 "${original_history}" history_digest)
    if(NOT history_digest STREQUAL "f083c1df7d968c44d58ebbf820b7c3c86dac648d59cf83ea835585d52683de50")
        message(FATAL_ERROR "Pinned Studio history implementation changed")
    endif()
    file(READ "${original_history}" history_code)
    string(REPLACE "#include \"history.h\"" "#include \"ext/history.h\"" history_code "${history_code}")
    string(FIND "${history_code}" "typedef struct\n{" history_types)
    string(FIND "${history_code}" "static void list_delete(" history_functions)
    math(EXPR history_length "${history_functions} - ${history_types}")
    string(SUBSTRING "${history_code}" ${history_types} ${history_length} history_block)
    string(REPLACE "${history_block}" "#include \"tic80_mister/history_private.h\"\ntypedef tm_history_data Data;\ntypedef tm_history_item Item;\n\n" history_code "${history_code}")
    string(FIND "${history_code}" "struct History\n{" history_type)
    string(FIND "${history_code}" "History* history_create(" history_create)
    math(EXPR history_length "${history_create} - ${history_type}")
    string(SUBSTRING "${history_code}" ${history_type} ${history_length} history_block)
    string(REPLACE "${history_block}" "static uint64_t tm_revision;\nuint64_t tm_history_revision(const History* h) { return h ? h->revision : 0; }\nvoid tm_history_touch(History* h) { h->revision = ++tm_revision; }\n\n" history_code "${history_code}")
    string(REPLACE "    return history;" "    tm_history_touch(history);\n    return history;" history_code "${history_code}")
    string(REPLACE "    return true;" "    tm_history_touch(history);\n    return true;" history_code "${history_code}")
    string(REPLACE "history->list = history->list->prev;" "history->list = history->list->prev;\n        tm_history_touch(history);" history_code "${history_code}")
    string(REPLACE "history_diff(history, &history->list->data);" "history_diff(history, &history->list->data);\n        tm_history_touch(history);" history_code "${history_code}")
    set(staged_history "${CMAKE_BINARY_DIR}/studio_adapters/history.c")
    tm_write_generated("${staged_history}" "${history_code}")
    target_sources(tic80studio PRIVATE "${staged_history}" "${TM_PROJECT_SOURCE}/src/studio_history.c")
    # Clear-all changes the packed CodeState without adding an undo node.
    # Advance only the snapshot revision so recovery retains cleared marks.
    set(original_code "${TM_TIC80_SOURCE}/src/studio/editors/code.c")
    file(SHA256 "${original_code}" code_digest)
    if(NOT code_digest STREQUAL "a8b5f689d73afd2565879ac5518b31b9272f1a5610d3e4f9b58b0e8b76276e6e")
        message(FATAL_ERROR "Pinned Studio code editor implementation changed")
    endif()
    file(READ "${original_code}" editor_code)
    # Retain Drag until its hide transition finishes, so this same key cannot
    # also leave Vi Insert/Select/Seek in the editor tick after global dispatch.
    set(drag_escape [=[    case TEXT_DRAG_CODE:
        setCodeMode(code, TEXT_EDIT_MODE);
        break;]=])
    string(FIND "${editor_code}" "${drag_escape}" drag_escape_at)
    if(drag_escape_at LESS 0)
        message(FATAL_ERROR "Pinned Studio Drag Escape changed")
    endif()
    string(REPLACE "${drag_escape}" [=[    case TEXT_DRAG_CODE:
        code->scroll.active = false;
        code->cursor.mouseDownPosition = NULL;
        code->anim.movie = resetMovie(&code->anim.hide);
        break;]=] editor_code "${editor_code}")
    set(drag_tick [=[static void textDragTick(Code* code)
{
    tic_mem* tic = code->tic;

    processMouse(code);]=])
    string(FIND "${editor_code}" "${drag_tick}" drag_tick_at)
    if(drag_tick_at LESS 0)
        message(FATAL_ERROR "Pinned Studio Drag input dispatch changed")
    endif()
    string(REPLACE "${drag_tick}" [=[static void textDragTick(Code* code)
{
    tic_mem* tic = code->tic;

    if(code->anim.movie != &code->anim.hide) processMouse(code);]=] editor_code "${editor_code}")
    string(REPLACE "#include \"code.h\"" "#include \"studio/editors/code.h\"" editor_code "${editor_code}")
    string(PREPEND editor_code "#include \"tic80_mister/history_private.h\"\n#include \"tic80_mister/studio.h\"\n")
    set(clear_bookmarks [=[            for(CodeState* s = code->state, *end = s + TIC_CODE_SIZE; s != end; ++s)
                s->bookmark = 0;]=])
    string(FIND "${editor_code}" "${clear_bookmarks}" clear_bookmarks_at)
    if(clear_bookmarks_at LESS 0)
        message(FATAL_ERROR "Pinned Studio clear-bookmark operation changed")
    endif()
    string(REPLACE "${clear_bookmarks}" [=[            bool tm_cleared = false;
            for(CodeState* s = code->state, *end = s + TIC_CODE_SIZE; s != end; ++s)
                if(s->bookmark) { s->bookmark = 0; tm_cleared = true; }
            if(tm_cleared) tm_history_touch(code->history);]=] editor_code "${editor_code}")
    set(deferred_history [=[    if (checkStudioViMode(code->studio, VI_INSERT))
        return;]=])
    string(FIND "${editor_code}" "${deferred_history}" deferred_history_at)
    if(deferred_history_at LESS 0)
        message(FATAL_ERROR "Pinned Studio Vi insert history operation changed")
    endif()
    string(REPLACE "${deferred_history}" [=[    if (checkStudioViMode(code->studio, VI_INSERT))
    {
        // Snapshot in-progress text/bookmarks without committing an undo node.
        // Leaving Insert still creates the single upstream history group.
        packState(code);
        tm_history_touch(code->history);
        return;
    }]=] editor_code "${editor_code}")
    set(replace_label [=[            code->popup.offset = code->popup.text + strlen(code->popup.text);
            strcat(code->popup.text, " WITH:");]=])
    string(FIND "${editor_code}" "${replace_label}" replace_label_at)
    if(replace_label_at LESS 0)
        message(FATAL_ERROR "Pinned Studio replace label changed")
    endif()
    string(REPLACE "${replace_label}" [=[            if(strlen(code->popup.text) + sizeof " WITH:" <= sizeof code->popup.text)
            {
                code->popup.offset = code->popup.text + strlen(code->popup.text);
                strcat(code->popup.text, " WITH:");
            }
            else tm_studio_popup(code->studio, "Search too long for replace");]=] editor_code "${editor_code}")
    set(goto_parse [=[    s32 line = atoi(code->popup.text);

    if(line) line--;

    s32 count = getLinesCount(code);]=])
    string(FIND "${editor_code}" "${goto_parse}" goto_parse_at)
    if(goto_parse_at LESS 0)
        message(FATAL_ERROR "Pinned Studio goto number parser changed")
    endif()
    string(REPLACE "${goto_parse}" [=[    s32 count = getLinesCount(code), line = 0;
    // GOTO accepts decimal input; saturate at the last one-based line rather
    // than overflowing atoi on a full popup of digits.
    for(const char* p = code->popup.text; *p; ++p)
    {
        if(*p < '0' || *p > '9') break;
        line = MIN(line * 10 + (*p - '0'), count + 1);
    }
    if(line) line--;]=] editor_code "${editor_code}")
    set(mode_fragment "${TM_PROJECT_SOURCE}/src/studio_code_view.inc")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mode_fragment}")
    file(READ "${mode_fragment}" mode_code)
    string(APPEND editor_code "\n${mode_code}")
    set(staged_code "${CMAKE_BINARY_DIR}/studio_adapters/editors/code.c")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/studio_adapters/editors")
    tm_write_generated("${staged_code}" "${editor_code}")
    get_target_property(code_sources tic80studio SOURCES)
    list(REMOVE_ITEM code_sources "${original_code}")
    set_property(TARGET tic80studio PROPERTY SOURCES "${code_sources}")
    target_sources(tic80studio PRIVATE "${staged_code}")
    target_include_directories(tic80studio PRIVATE "${TM_PROJECT_SOURCE}/include")
    target_include_directories(tic80studio PRIVATE "${TM_TIC80_SOURCE}/src/studio/screens")
    target_link_libraries(tic80studio PRIVATE tic80_cart_file)
    # Managed sessions load the service's checked save format and persist only
    # acknowledged parent snapshots. Offscreen/unmanaged Studio keeps upstream
    # storage. Never write persistent memory directly from a supervised tick.
    file(READ "${TM_TIC80_SOURCE}/src/studio/screens/run.c" run_code)
    string(PREPEND run_code "#include \"tic80_mister/studio.h\"\n")
    set(pmem_tick "if(memcmp(run->pmem.data, tic->ram->persistent.data, Size))")
    set(pmem_load "        s32 size = 0;\n        void* data = tic_fs_loadroot(run->fs, run->saveid, &size);")
    set(pmem_end "            memcpy(&run->tic->ram->persistent, data, MIN(size, Size));\n        }")
    foreach(needle IN ITEMS "${pmem_tick}" "${pmem_load}" "${pmem_end}")
        string(FIND "${run_code}" "${needle}" found)
        if(found LESS 0)
            message(FATAL_ERROR "Pinned Studio persistent-memory code changed")
        endif()
    endforeach()
    string(REPLACE "${pmem_tick}" "if(!tm_studio_pmem_managed() && memcmp(run->pmem.data, tic->ram->persistent.data, Size))" run_code "${run_code}")
    string(REPLACE "${pmem_load}" "        int managed = tm_studio_pmem_load(studio);\n        if(managed < 0) onError(run, \"Cannot load persistent memory; save file preserved\");\n        if(!managed)\n        {\n${pmem_load}" run_code "${run_code}")
    string(REPLACE "${pmem_end}" "${pmem_end}\n        }" run_code "${run_code}")
    set(staged_run "${CMAKE_BINARY_DIR}/studio_adapters/screens/run.c")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/studio_adapters/screens")
    tm_write_generated("${staged_run}" "${run_code}")
    get_target_property(run_sources tic80studio SOURCES)
    list(REMOVE_ITEM run_sources "${TM_TIC80_SOURCE}/src/studio/screens/run.c")
    set_property(TARGET tic80studio PROPERTY SOURCES "${run_sources}")
    target_sources(tic80studio PRIVATE "${staged_run}")
    # Notify hash-load consumers on failure; preserve the previous cartridge
    # and let the console/SURF callbacks finish without starting a failed load.
    foreach(unit fs rom screens/console screens/surf)
        file(READ "${TM_TIC80_SOURCE}/src/studio/${unit}.c" unit_code)
        string(PREPEND unit_code "#include \"tic80_mister/studio.h\"\n")
        if(unit STREQUAL "fs")
            set(cache_guard "    if (netData->type == net_get_done)\n    {\n        tic_fs_saveroot(loadFileByHashData->fs")
            set(cache_guard_replacement "    bool valid = netData->type == net_get_done && tm_studio_cart_data_valid(netData->done.data, netData->done.size);\n    if (netData->type == net_get_done && !valid)\n        loadFileByHashData->done(NULL, 0, loadFileByHashData->data);\n\n    if (valid)\n    {\n        tic_fs_saveroot(loadFileByHashData->fs")
            string(FIND "${unit_code}" "${cache_guard}" cache_at)
            if(cache_at LESS 0)
                message(FATAL_ERROR "Pinned Studio cache write changed")
            endif()
            string(REPLACE "${cache_guard}" "${cache_guard_replacement}" unit_code "${unit_code}")
            set(cached_load "        if (buffer)\n        {\n            callback(buffer, size, data);\n            free(buffer);\n            return;\n        }")
            set(cached_load_replacement "        if (buffer)\n        {\n            bool valid = tm_studio_cart_data_valid(buffer, size);\n            if(valid) callback(buffer, size, data);\n            free(buffer);\n            if(valid) return;\n            tic_remove(tic_fs_pathroot(fs, cachePath));\n        }")
            string(FIND "${unit_code}" "${cached_load}" cached_at)
            if(cached_at LESS 0)
                message(FATAL_ERROR "Pinned Studio cached load changed")
            endif()
            string(REPLACE "${cached_load}" "${cached_load_replacement}" unit_code "${unit_code}")
            set(needle "    switch (netData->type)\n    {\n    case net_get_done:\n    case net_get_error:\n\n        free(loadFileByHashData->cachePath);")
            set(replacement "    if (netData->type == net_get_error)\n        loadFileByHashData->done(NULL, 0, loadFileByHashData->data);\n\n${needle}")
        elseif(unit STREQUAL "rom")
            string(PREPEND unit_code "#include <limits.h>\n")
            set(name_suffix [=[    strcpy(path, name);

    size_t ps = strlen(path);
    size_t es = strlen(ext);

    if(!(ps > es && strstr(path, ext) + es == path + ps))
        strcat(path, ext);]=])
            string(FIND "${unit_code}" "${name_suffix}" name_suffix_at)
            if(name_suffix_at LESS 0)
                message(FATAL_ERROR "Pinned Studio filename suffix changed")
            endif()
            string(REPLACE "${name_suffix}" [=[    size_t ps = strlen(name);
    size_t es = tic_tool_has_ext(name, ext) ? 0 : strlen(ext);
    // Preserve an overlong name for the caller to reject, never truncate it
    // into a different file or overflow the static filename buffer.
    if(ps >= sizeof path || es >= sizeof path - ps) return name;
    memcpy(path, name, ps + 1);
    if(es) memcpy(path + ps, ext, es + 1);]=] unit_code "${unit_code}")
            set(file_load "    void* data = tic_fs_load(studio_fs(studio), path, &size);\n\n    if(!data)\n        data = fs_read(path, &size);")
            string(FIND "${unit_code}" "${file_load}" file_load_at)
            if(file_load_at LESS 0)
                message(FATAL_ERROR "Pinned Studio file load changed")
            endif()
            string(REPLACE "${file_load}" [=[    char tm_source[TICNAME_MAX], tm_resolved[PATH_MAX];
    if(!tm_studio_cart_destination(studio, path, false, tm_source)) return false;
    void* data = tic_fs_load(studio_fs(studio), path, &size);
    if(!data)
    {
        if(!realpath(path, tm_resolved) || strlen(tm_resolved) >= TICNAME_MAX) return false;
        strcpy(tm_source, tm_resolved);
        data = fs_read(tm_source, &size);
    }
    if(data && (!realpath(tm_source, tm_resolved) || strlen(tm_resolved) >= TICNAME_MAX))
    {
        free(data); return false;
    }
    if(data) strcpy(tm_source, tm_resolved);
]=] unit_code "${unit_code}")
            string(REPLACE "        studioSetCartName(studio, cartName, path);" "        if((tic_tool_has_ext(cartName, CART_EXT) || tic_tool_has_ext(cartName, PNG_EXT)) && !tm_studio_cart_data_valid(data, size)) { free(data); return false; }" unit_code "${unit_code}")
            set(load_finish "    if(done)\n        studioRomLoaded(studio);")
            string(FIND "${unit_code}" "${load_finish}" load_finish_at)
            if(load_finish_at LESS 0)
                message(FATAL_ERROR "Pinned Studio file load completion changed")
            endif()
            string(REPLACE "${load_finish}" "    if(done)\n    {\n        const char* tm_name = strrchr(tm_source, '/');\n        studioSetCartName(studio, tm_name ? tm_name + 1 : tm_source, tm_source);\n        studioRomLoaded(studio);\n    }" unit_code "${unit_code}")
            set(save_begin "CartSaveResult studioSaveCart(Studio* studio, const char* name)\n{")
            set(save_write "if(size && tic_fs_save(studio_fs(studio), name, buffer, size, true))")
            set(save_name "studioSetCartName(studio, name, tic_fs_path(studio_fs(studio), name));")
            foreach(needle IN ITEMS "${save_begin}" "${save_write}" "${save_name}")
                string(FIND "${unit_code}" "${needle}" save_at)
                if(save_at LESS 0)
                    message(FATAL_ERROR "Pinned Studio cartridge save changed")
                endif()
            endforeach()
            string(REPLACE "${save_begin}" "${save_begin}\n    bool tm_use_source = !name || !*name;\n    if(tm_use_source) name = studioCart(studio)->name;\n    if(strlen(name) >= TICNAME_MAX - sizeof CART_EXT) return CART_SAVE_ERROR;\n    char tm_destination[TICNAME_MAX];" unit_code "${unit_code}")
            string(REPLACE "${save_write}" "if(size && tm_studio_cart_save(studio, name, tm_use_source, buffer, size, tm_destination))" unit_code "${unit_code}")
            string(REPLACE "${save_name}" "studioSetCartName(studio, strrchr(tm_destination, '/') ? strrchr(tm_destination, '/') + 1 : tm_destination, tm_destination);" unit_code "${unit_code}")
            set(png_assignment "                    buffer = result.data;\n                    size = result.size;")
            string(FIND "${unit_code}" "${png_assignment}" png_assignment_at)
            if(png_assignment_at LESS 0)
                message(FATAL_ERROR "Pinned Studio PNG save buffer assignment changed")
            endif()
            string(REPLACE "${png_assignment}" "                    free(buffer);\n${png_assignment}" unit_code "${unit_code}")
            string(FIND "${unit_code}" "static void hashLoadDone(" begin)
            string(FIND "${unit_code}" "void studioLoadByHash(" end)
            if(begin LESS 0 OR end LESS begin)
                message(FATAL_ERROR "Pinned hash-load function changed")
            endif()
            math(EXPR length "${end} - ${begin}")
            string(SUBSTRING "${unit_code}" ${begin} ${length} needle)
            set(replacement "static void hashLoadDone(const u8* buffer, s32 size, void* data)\n{\n    LoadByHashData* load = data;\n    tm_studio_hashload_apply(load->studio, buffer, size, load->name, load->section);\n    if(load->callback) load->callback(load->calldata);\n    FREE(load->name); FREE(load->section); FREE(load);\n}\n\n")
        elseif(unit STREQUAL "screens/console")
            set(needle "    printCartLoaded(console);\n\n    if(load->callback)")
            set(replacement "    if(tm_studio_hashload_succeeded(console->studio)) printCartLoaded(console);\n    else printError(console, \"cart downloading error :(\");\n\n    if(load->callback)")
        else()
            set(needle "static void onCartLoaded(void* data)\n{\n    Surf* surf = data;")
            set(replacement "${needle}\n    if(!tm_studio_hashload_succeeded(surf->studio)) {\n        surf->anim.movie = resetMovie(&surf->anim.idle);\n        return;\n    }")
        endif()
        string(FIND "${unit_code}" "${needle}" found)
        if(found LESS 0)
            message(FATAL_ERROR "Pinned Studio ${unit} network callback changed")
        endif()
        string(REPLACE "${needle}" "${replacement}" unit_code "${unit_code}")
        set(staged_unit "${CMAKE_BINARY_DIR}/studio_adapters/${unit}.c")
        get_filename_component(staged_dir "${staged_unit}" DIRECTORY)
        file(MAKE_DIRECTORY "${staged_dir}")
        tm_write_generated("${staged_unit}" "${unit_code}")
        get_target_property(unit_sources tic80studio SOURCES)
        list(REMOVE_ITEM unit_sources "${TM_TIC80_SOURCE}/src/studio/${unit}.c")
        set_property(TARGET tic80studio PROPERTY SOURCES "${unit_sources}")
        target_sources(tic80studio PRIVATE "${staged_unit}")
    endforeach()
    target_compile_definitions(tic80studio PRIVATE __LIBRETRO__)
    target_link_libraries(tic80studio PRIVATE tic80_spawn)
    add_library(tic80_studio_system STATIC "${TM_PROJECT_SOURCE}/src/studio_system.c")
    target_include_directories(tic80_studio_system PUBLIC "${TM_PROJECT_SOURCE}/include")
    target_link_libraries(tic80_studio_system PUBLIC tic80studio)
    add_library(tic80_studio_session STATIC "${TM_PROJECT_SOURCE}/src/studio_session.c")
    target_link_libraries(tic80_studio_session PUBLIC tic80_studio_system tic80_memory_equal tic80_cart_guard tic80_pmem tic80_fft_worker Threads::Threads)
endfunction()
