/* MRuby owns its VM per core; upstream TIC API calls use CurrentMachine.
 * Select the caller's core before every interpreter entry. */
#include "core/core.h"
static bool tm_ruby_init(tic_mem *, const char *);
static void tm_ruby_close(tic_mem *);
static void tm_ruby_tick(tic_mem *);
static void tm_ruby_boot(tic_mem *);
static void tm_ruby_scn(tic_mem *, s32, void *);
static void tm_ruby_bdr(tic_mem *, s32, void *);
static void tm_ruby_menu(tic_mem *, s32, void *);
static void tm_ruby_eval(tic_mem *, const char *);
#include TM_UPSTREAM_ADAPTER
static bool tm_ruby_init(tic_mem *tic,const char *code) { return initMRuby(tic,code); }
static void tm_ruby_close(tic_mem *tic) { CurrentMachine=(tic_core *)tic; closeMRuby(tic); }
static void tm_ruby_tick(tic_mem *tic) { CurrentMachine=(tic_core *)tic; callMRubyTick(tic); }
static void tm_ruby_boot(tic_mem *tic) { CurrentMachine=(tic_core *)tic; callMRubyBoot(tic); }
static void tm_ruby_scn(tic_mem *tic,s32 row,void *data) { CurrentMachine=(tic_core *)tic; callMRubyScanline(tic,row,data); }
static void tm_ruby_bdr(tic_mem *tic,s32 row,void *data) { CurrentMachine=(tic_core *)tic; callMRubyBorder(tic,row,data); }
static void tm_ruby_menu(tic_mem *tic,s32 row,void *data) { CurrentMachine=(tic_core *)tic; callMRubyMenu(tic,row,data); }
static void tm_ruby_eval(tic_mem *tic,const char *code) { CurrentMachine=(tic_core *)tic; evalMRuby(tic,code); }
