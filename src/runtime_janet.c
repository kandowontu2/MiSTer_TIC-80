/* Preserve both Janet's VM and the upstream adapter's globals per cartridge.
 * The service can preflight another Janet cart and still resume its old VM. */
#include "core/core.h"
static bool tm_janet_init(tic_mem *, const char *);
static void tm_janet_close(tic_mem *);
static void tm_janet_tick(tic_mem *);
static void tm_janet_boot(tic_mem *);
static void tm_janet_scn(tic_mem *, s32, void *);
static void tm_janet_bdr(tic_mem *, s32, void *);
static void tm_janet_menu(tic_mem *, s32, void *);
static void tm_janet_eval(tic_mem *, const char *);
#include TM_UPSTREAM_ADAPTER
typedef struct machine {
    tic_mem *tic;
    JanetVM *vm;
    JanetFiber *fiber;
    JanetBuffer *errors;
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
        janet_vm_load(m->vm);
        CurrentMachine=(tic_core *)tic;
        GameFiber=m->fiber;
        errBuffer=m->errors;
    }
    return m;
}
static void save_machine(machine *m)
{
    janet_vm_save(m->vm);
    m->fiber=GameFiber;
    m->errors=errBuffer;
}
static bool tm_janet_init(tic_mem *tic,const char *code)
{
    tm_janet_close(tic);
    machine *m=calloc(1,sizeof *m);
    if(!m) return false;
    m->tic=tic; m->vm=janet_vm_alloc(); m->next=machines; machines=m;
    bool result=initJanet(tic,code);
    save_machine(m);
    return result;
}
static void tm_janet_close(tic_mem *tic)
{
    machine *m=select_machine(tic);
    if(!m) return;
    closeJanet(tic);
    machine **link=&machines;
    while(*link!=m) link=&(*link)->next;
    *link=m->next;
    janet_vm_free(m->vm);
    free(m);
}
#define CALL0(wrapper,original) static void wrapper(tic_mem *tic) { machine *m=select_machine(tic); if(m) { original(tic); save_machine(m); } }
#define CALL1(wrapper,original) static void wrapper(tic_mem *tic,s32 value,void *data) { machine *m=select_machine(tic); if(m) { original(tic,value,data); save_machine(m); } }
CALL0(tm_janet_tick,callJanetTick)
CALL0(tm_janet_boot,callJanetBoot)
CALL1(tm_janet_scn,callJanetScanline)
CALL1(tm_janet_bdr,callJanetBorder)
CALL1(tm_janet_menu,callJanetMenu)
static void tm_janet_eval(tic_mem *tic,const char *code) { machine *m=select_machine(tic); if(m) { evalJanet(tic,code); save_machine(m); } }
