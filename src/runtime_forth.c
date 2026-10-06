/* pForth exposes its interpreter state as globals. Save it per cartridge;
 * malloc and paging-disabled builds have no shared allocator/page state. */
#include "core/core.h"
static bool tm_forth_init(tic_mem *, const char *);
static void tm_forth_close(tic_mem *);
static void tm_forth_tick(tic_mem *);
static void tm_forth_boot(tic_mem *);
static void tm_forth_scn(tic_mem *, s32, void *);
static void tm_forth_bdr(tic_mem *, s32, void *);
static void tm_forth_menu(tic_mem *, s32, void *);
static void tm_forth_eval(tic_mem *, const char *);
#include TM_UPSTREAM_ADAPTER
extern cell_t gPfAssertEnabled;
#define GLOBALS(X) \
    X(gCurrentTask) X(gCurrentDictionary) X(gNumPrimitives) \
    X(gLocalCompiler_XT) X(gNumberQ_XT) X(gQuitP_XT) X(gAcceptP_XT) \
    X(gPfAssertEnabled) X(gDepthAtColon) X(gVarContext) X(gVarState) \
    X(gVarBase) X(gVarByeCode) X(gVarEcho) X(gVarTraceLevel) \
    X(gVarTraceStack) X(gVarTraceFlags) X(gVarQuiet) X(gVarReturnCode) \
    X(gIncludeIndex) X(gIncludeStack) X(gScratch) X(gForthTask) X(gOutBuf) X(gOutLen)
typedef struct machine {
    tic_mem *tic;
    cell_t callback_context;
    bool callbacks_cached,scn,bdr,menu;
#define FIELD(name) __typeof__(name) name;
    GLOBALS(FIELD)
#undef FIELD
    struct machine *next;
} machine;
static machine *machines;
static machine *find(tic_mem *tic)
{
    machine *m=machines;
    while(m && m->tic!=tic) m=m->next;
    return m;
}
static machine *select_machine(tic_mem *tic)
{
    machine *m=find(tic);
    if(m) {
#define LOAD(name) memcpy(&name,&m->name,sizeof name);
        GLOBALS(LOAD)
#undef LOAD
        gForthCore=(tic_core *)tic;
        gTickData=gForthCore->data;
    }
    return m;
}
static void save_machine(machine *m)
{
    // Missing optional callbacks used to search the entire Forth dictionary
    // on every scanline/border row. Refresh when definitions change, so a
    // callback created by EVALUATE during TIC also takes effect immediately.
    if(((tic_core *)m->tic)->currentVM &&
            (!m->callbacks_cached || m->callback_context!=gVarContext)) {
        ExecToken token;
        m->scn=ffFindC(SCN_FN,&token)!=0;
        m->bdr=ffFindC(BDR_FN,&token)!=0;
        m->menu=ffFindC(MENU_FN,&token)!=0;
        m->callback_context=gVarContext;
        m->callbacks_cached=true;
    }
#define SAVE(name) memcpy(&m->name,&name,sizeof name);
    GLOBALS(SAVE)
#undef SAVE
}
static bool tm_forth_init(tic_mem *tic,const char *code)
{
    tm_forth_close(tic);
    machine *m=calloc(1,sizeof *m);
    if(!m) return false;
    m->tic=tic; m->next=machines; machines=m;
    bool result=initForth(tic,code);
    save_machine(m);
    return result;
}
static void tm_forth_close(tic_mem *tic)
{
    machine *m=select_machine(tic);
    if(!m) return;
    closeForth(tic);
    machine **link=&machines;
    while(*link!=m) link=&(*link)->next;
    *link=m->next;
    free(m);
}
#define CALL0(wrapper,original) static void wrapper(tic_mem *tic) { machine *m=select_machine(tic); if(m) { original(tic); save_machine(m); } }
CALL0(tm_forth_tick,callForthTick)
CALL0(tm_forth_boot,callForthBoot)
static void tm_forth_scn(tic_mem *tic,s32 row,void *data) { machine *m=find(tic); if(m && m->scn) { select_machine(tic); callForthScanline(tic,row,data); save_machine(m); } }
static void tm_forth_bdr(tic_mem *tic,s32 row,void *data) { machine *m=find(tic); if(m && m->bdr) { select_machine(tic); callForthBorder(tic,row,data); save_machine(m); } }
static void tm_forth_menu(tic_mem *tic,s32 row,void *data) { machine *m=find(tic); if(m && m->menu) { select_machine(tic); callForthMenu(tic,row,data); save_machine(m); } }
static void tm_forth_eval(tic_mem *tic,const char *code) { machine *m=select_machine(tic); if(m) { evalForth(tic,code); save_machine(m); } }
