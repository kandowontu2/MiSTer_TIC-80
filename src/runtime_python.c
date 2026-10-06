/* PocketPy's upstream TIC-80 adapter assumes one active VM. Select a distinct
 * library VM for each cartridge, so candidate validation and failed loads do
 * not replace/reset the currently running game. Calls stay on the game thread. */
#include "core/core.h"
static bool tm_python_init(tic_mem *, const char *);
static void tm_python_close(tic_mem *);
static void tm_python_tick(tic_mem *);
static void tm_python_boot(tic_mem *);
static void tm_python_scn(tic_mem *, s32, void *);
static void tm_python_bdr(tic_mem *, s32, void *);
static void tm_python_menu(tic_mem *, s32, void *);
static void tm_python_eval(tic_mem *, const char *);
#include TM_UPSTREAM_ADAPTER
static tic_mem *machines[16];
static int find(tic_mem *tic)
{
    for (int i=0;i<16;++i) if (machines[i]==tic) return i;
    return -1;
}
static int select_machine(tic_mem *tic)
{
    int slot=find(tic);
    if (slot<0) return 0;
    py_switchvm(slot);
    return 1;
}
static bool tm_python_init(tic_mem *tic, const char *code)
{
    tm_python_close(tic);
    py_initialize();
    for (int slot=0;slot<16;++slot) if (!machines[slot]) {
        machines[slot]=tic;
        py_switchvm(slot);
        return init_pkpy_v2(tic,code);
    }
    tic_core *core=(tic_core *)tic;
    core->data->error(core->data->data,"No free PocketPy cartridge VM");
    return false;
}
static void tm_python_close(tic_mem *tic)
{
    int slot=find(tic);
    if (slot<0) return;
    py_switchvm(slot);
    close_pkpy_v2(tic);
    machines[slot]=NULL;
}
static void tm_python_tick(tic_mem *tic) { if(select_machine(tic)) tick_pkpy_v2(tic); }
static void tm_python_boot(tic_mem *tic) { if(select_machine(tic)) boot_pkpy_v2(tic); }
static void tm_python_scn(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callback_scanline(tic,row,data); }
static void tm_python_bdr(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callback_border(tic,row,data); }
static void tm_python_menu(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callback_menu(tic,row,data); }
static void tm_python_eval(tic_mem *tic,const char *code) { if(select_machine(tic)) eval_pkpy_v2(tic,code); }
