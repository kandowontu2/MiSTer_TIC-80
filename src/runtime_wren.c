/* Wren VMs are independent, but upstream's TIC-80 adapter keeps game/call
 * handles globally. Restore the handles belonging to each cartridge. */
#include "core/core.h"
#include "wren_vm.h"
static bool tm_wren_init(tic_mem *, const char *);
static bool tm_wren_has_overline(tic_mem *);
static void tm_wren_close(tic_mem *);
static void tm_wren_tick(tic_mem *);
static void tm_wren_boot(tic_mem *);
static void tm_wren_scn(tic_mem *, s32, void *);
static void tm_wren_bdr(tic_mem *, s32, void *);
static void tm_wren_menu(tic_mem *, s32, void *);
#include TM_UPSTREAM_ADAPTER
typedef struct machine {
    tic_mem *tic;
    WrenHandle *game,*create,*update,*boot,*scn,*bdr,*menu,*ovr;
    bool loaded, has_overline;
    struct machine *next;
} machine;
static machine *machines;
static machine *find(tic_mem *tic)
{
    for(machine *m=machines;m;m=m->next) if(m->tic==tic) return m;
    return NULL;
}
static bool tm_wren_has_overline(tic_mem *tic)
{
    machine *m=find(tic);
    return m && m->has_overline;
}
static bool overrides_overline(tic_mem *tic,WrenHandle *game)
{
    /* Wren copies inherited methods into each class's dispatch table. Compare
     * the actual closure with TIC's empty default, including overrides inherited
     * through an intermediate class. Source text searches cannot do this. */
    WrenVM *vm=((tic_core *)tic)->currentVM;
    int symbol=wrenSymbolTableFind(&vm->methodNames,OVR_FN "()",sizeof(OVR_FN "()")-1);
    if(!game || symbol<0) return false;
    ObjClass *klass=wrenGetClass(vm,game->value);
    wrenEnsureSlots(vm,1);
    wrenGetVariable(vm,"main","TIC",0);
    ObjClass *base=AS_CLASS(vm->apiStack[0]);
    if(symbol>=klass->methods.count || symbol>=base->methods.count) return false;
    Method actual=klass->methods.data[symbol], inherited=base->methods.data[symbol];
    return actual.type!=METHOD_NONE && (actual.type!=METHOD_BLOCK ||
        inherited.type!=METHOD_BLOCK || actual.as.closure!=inherited.as.closure);
}
static machine *select_machine(tic_mem *tic)
{
    machine *m=find(tic);
    if(m) {
        game_class=m->game; new_handle=m->create; update_handle=m->update;
        boot_handle=m->boot; scanline_handle=m->scn; border_handle=m->bdr;
        menu_handle=m->menu; overline_handle=m->ovr; loaded=m->loaded;
    }
    return m;
}
static bool tm_wren_init(tic_mem *tic,const char *code)
{
    tm_wren_close(tic);
    machine *m=calloc(1,sizeof *m);
    if(!m) return false;
    m->tic=tic; m->next=machines; machines=m;
    select_machine(tic);
    bool result=initWren(tic,code);
    m->game=game_class; m->create=new_handle; m->update=update_handle;
    m->boot=boot_handle; m->scn=scanline_handle; m->bdr=border_handle;
    m->menu=menu_handle; m->ovr=overline_handle; m->loaded=loaded;
    if(result) m->has_overline=overrides_overline(tic,m->game);
    return result;
}
static void tm_wren_close(tic_mem *tic)
{
    machine *m=select_machine(tic);
    if(!m) return;
    closeWren(tic);
    machine **link=&machines;
    while(*link!=m) link=&(*link)->next;
    *link=m->next;
    free(m);
}
static void tm_wren_tick(tic_mem *tic) { if(select_machine(tic)) callWrenTick(tic); }
static void tm_wren_boot(tic_mem *tic) { if(select_machine(tic)) callWrenBoot(tic); }
static void tm_wren_scn(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callWrenScanline(tic,row,data); }
static void tm_wren_bdr(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callWrenBorder(tic,row,data); }
static void tm_wren_menu(tic_mem *tic,s32 row,void *data) { if(select_machine(tic)) callWrenMenu(tic,row,data); }
