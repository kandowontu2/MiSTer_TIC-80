#ifndef TIC80_MISTER_STUDIO_H
#define TIC80_MISTER_STUDIO_H
#include "studio/system.h"
/* Platform state is owned by one Studio process. Editor clipboard operations
 * remain within that process; no desktop clipboard or window is required. */
void tm_studio_bind(Studio *studio);
/* Feed input once per frame, including latched Caps Lock state for text. */
void tm_studio_tick(Studio *studio, tic80_input input);
/* Exported from the staged pinned Studio implementation. */
void tm_studio_popup(Studio *studio, const char *text);
/* Resolve an owned confirmation as NO, including its callback allocation. */
void tm_studio_cancel_confirmation(Studio *studio);
bool tm_studio_hashload_succeeded(Studio *studio);
void tm_studio_hashload_result(Studio *studio, bool success);
void tm_studio_saved_hash(Studio *studio, u8 out[16]);
void tm_studio_restore_saved_hash(Studio *studio, const u8 hash[16]);
/* Selected sprite/map/SFX/music banks and the bank selector's display mode. */
typedef struct {
    u8 indexes[4];
    bool show, chained;
} tm_studio_banks;
void tm_studio_bank_state(Studio *studio, tm_studio_banks *out);
bool tm_studio_restore_banks(Studio *studio, const tm_studio_banks *banks);
enum { TM_SPRITE_ADVANCED=1, TM_SPRITE_HEX_INDEX=2, TM_SPRITE_PALETTE_BANK1=4 };
/* Persistent sprite preferences; process pointers and active drags are omitted. */
typedef struct {
    /* x/y are tile coordinates; bank/page/bpp describe the tile-sheet view. */
    u8 x, y, size, brush_size, color, color2, bpp, bank, page, tool, flags;
} tm_studio_sprite_view;
bool tm_studio_sprite_view_valid(const tm_studio_sprite_view *view);
void tm_studio_sprite_views(Studio *studio, tm_studio_sprite_view out[8]);
bool tm_studio_restore_sprite_views(Studio *studio, const tm_studio_sprite_view views[8]);
enum { TM_CODE_ALT_FONT=1, TM_CODE_SHADOW=2 };
enum { TM_CODE_DRAG, TM_CODE_FIND, TM_CODE_GOTO, TM_CODE_BOOKMARK, TM_CODE_OUTLINE,
       TM_CODE_REPLACE, TM_CODE_EDIT, TM_CODE_POPUP_BYTES=34 };
enum { TM_CODE_IDLE, TM_CODE_SHOW, TM_CODE_HIDE };
/* Text positions are byte offsets into cart.code; selection=-1 means absent.
 * column retains the desired column when moving through shorter lines. */
typedef struct {
    s32 cursor, selection, column, scroll_x, scroll_y;
    u32 flags, vi_mode; /* Pinned ViMode: Normal, Insert, Select, Seek, SeekBack. */
    u32 mode, animation;
    s32 previous_cursor, previous_selection, replace_offset, jump_line;
    s32 sidebar_count, sidebar_index, sidebar_scroll;
    s32 animation_tick, popup_y, sidebar_x;
    char popup_text[TM_CODE_POPUP_BYTES];
    u8 reserved[2];
} tm_studio_code_view;
bool tm_studio_code_view_valid(const tm_studio_code_view *view, size_t code_length);
void tm_studio_code_state(Studio *studio, tm_studio_code_view *out);
bool tm_studio_restore_code(Studio *studio, const tm_studio_code_view *view);
uint64_t tm_studio_code_history_revision(Studio *studio);
int tm_studio_code_history_snapshot(Studio *studio,u32 request);
bool tm_studio_code_history_restore(Studio *studio,int descriptor);
enum { TM_STUDIO_HISTORY_COUNT=41 };
uint64_t tm_studio_history_revision(Studio *studio);
int tm_studio_history_snapshot(Studio *studio,u32 request);
bool tm_studio_history_snapshot_valid(int descriptor,u32 *request);
bool tm_studio_history_restore(Studio *studio,int descriptor);
/* Optional supervisor persistence. Load returns 0 for upstream storage, 1
 * when shared values were loaded, and -1 to reject a corrupt/unreadable save. */
typedef int (*tm_studio_pmem_loader)(Studio*, void*);
void tm_studio_pmem_hook(tm_studio_pmem_loader loader, void *data);
bool tm_studio_pmem_managed(void);
bool tm_studio_pmem_failed(void);
int tm_studio_pmem_load(Studio *studio);
void tm_studio_caps_state(bool *latched, bool *down);
void tm_studio_restore_caps(bool latched, bool down);
bool tm_studio_cart_data_valid(const u8 *bytes, s32 size);
/* Native/PNG file I/O runs in the supervised worker. The canonical source
 * path survives recovery and is used by default Save, independent of cwd. */
bool tm_studio_cart_file_apply(Studio *studio, const char *path);
bool tm_studio_cart_source(const char *path, const u8 *bytes, size_t size, char out[TICNAME_MAX]);
bool tm_studio_cart_write(const char *path, const void *bytes, s32 size);
/* A managed worker records the prepared file before atomic publication.
 * Returning false aborts the Save before rename. Standalone Studio has no hook. */
typedef bool (*tm_studio_cart_publish_guard)(Studio*, const char *binding,
    const char *target, int descriptor, const void *bytes, s32 size, void *data);
void tm_studio_cart_publish_hook(tm_studio_cart_publish_guard guard, void *data);
/* Record a newly created private temporary file before any writes. */
typedef bool (*tm_studio_cart_temp_guard)(const char *path, int descriptor, void *data);
void tm_studio_cart_temp_hook(tm_studio_cart_temp_guard guard, void *data);
/* An unbound OSD cart's first default Save creates a fresh working copy.
 * Existing names are skipped with atomic no-replace publication. A published
 * destination is retained even if its later sync fails; false still reports
 * Save failure and does not mark the cart saved. */
bool tm_studio_cart_save(Studio *studio, const char *name, bool use_source,
                        const void *bytes, s32 size, char out[TICNAME_MAX]);
bool tm_studio_cart_destination(Studio *studio, const char *name, bool use_source, char out[TICNAME_MAX]);
bool tm_studio_hashload_apply(Studio *studio, const u8 *bytes, s32 size,
                              const char *name, const char *section);
void tm_studio_clock(uint64_t nanoseconds); /* deterministic headless clock */
void tm_studio_real_clock(void);
#endif
