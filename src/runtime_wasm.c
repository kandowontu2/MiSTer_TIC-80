/* Upstream stores WASM export handles globally. Keep them with the owning
 * cartridge so two live modules cannot call exports in each other's memory. */
#include "core/core.h"
static bool tm_wasm_init(tic_mem *, const char *);
static void tm_wasm_close(tic_mem *);
static void tm_wasm_tick(tic_mem *);
static void tm_wasm_boot(tic_mem *);
static void tm_wasm_scn(tic_mem *, s32, void *);
static void tm_wasm_bdr(tic_mem *, s32, void *);
static void tm_wasm_menu(tic_mem *, s32, void *);
#include TM_UPSTREAM_ADAPTER
typedef struct machine {
    tic_mem *tic;
    IM3Function tick,boot,scn,bdr,menu;
    struct machine *next;
} machine;
static machine *machines;
static machine *find(tic_mem *tic)
{
    for(machine *m=machines;m;m=m->next) if(m->tic==tic) return m;
    return NULL;
}
static machine *select_machine(tic_mem *tic)
{
    machine *m=find(tic);
    if(m) {
        TIC_function=m->tick; BOOT_function=m->boot; SCN_function=m->scn;
        BDR_function=m->bdr; MENU_function=m->menu;
    }
    return m;
}
static bool tm_wasm_init(tic_mem *tic,const char *code)
{
    tm_wasm_close(tic);
    machine *m=calloc(1,sizeof *m);
    if(!m) return false;
    m->tic=tic; m->next=machines; machines=m;
    TIC_function=BOOT_function=SCN_function=BDR_function=MENU_function=NULL;
    bool result=initWasm(tic,code);
    m->tick=TIC_function; m->boot=BOOT_function; m->scn=SCN_function;
    m->bdr=BDR_function; m->menu=MENU_function;
    return result;
}
static void tm_wasm_close(tic_mem *tic)
{
    machine *m=select_machine(tic);
    if(!m) return;
    closeWasm(tic);
    machine **link=&machines;
    while(*link!=m) link=&(*link)->next;
    *link=m->next;
    free(m);
}
static void tm_wasm_tick(tic_mem *tic) { if(select_machine(tic)) callWasmTick(tic); }
static void tm_wasm_boot(tic_mem *tic) { if(select_machine(tic)) callWasmBoot(tic); }
static void tm_wasm_scn(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callWasmScanline(tic,row,data); }
static void tm_wasm_bdr(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callWasmBorder(tic,row,data); }
static void tm_wasm_menu(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callWasmMenu(tic,row,data); }
