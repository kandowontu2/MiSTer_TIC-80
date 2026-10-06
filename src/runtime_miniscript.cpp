/* TIC's MiniScript main harness yields after each frame. Synchronous raster
 * and menu callbacks must temporarily clear that flag: upstream RunFunction
 * otherwise repeatedly enters RunInner with yielding=true and never runs
 * the callback. Restore the harness's flag after a completed callback. */
#include "core/core.h"
static void tm_miniscript_scn(tic_mem *, s32, void *);
static void tm_miniscript_bdr(tic_mem *, s32, void *);
static void tm_miniscript_menu(tic_mem *, s32, void *);
#include TM_UPSTREAM_ADAPTER
static void tm_miniscript_callback(tic_mem *tic, s32 arg, void *data,
                                  void (*callback)(tic_mem *, s32, void *))
{
    auto *core = reinterpret_cast<tic_core *>(tic);
    auto *state = static_cast<TICMiniScriptState *>(core->currentVM);
    if (!state) return;
    auto machine = state->interpreter.vm();
    bool yielded = machine.yielding();
    machine.set_yielding(false);
    callback(tic, arg, data);
    machine.set_yielding(yielded);
}
static void tm_miniscript_scn(tic_mem *tic, s32 row, void *data) { tm_miniscript_callback(tic, row, data, callMiniScriptScanline); }
static void tm_miniscript_bdr(tic_mem *tic, s32 row, void *data) { tm_miniscript_callback(tic, row, data, callMiniScriptBorder); }
static void tm_miniscript_menu(tic_mem *tic, s32 index, void *data) { tm_miniscript_callback(tic, index, data, callMiniScriptMenu); }
