#define _GNU_SOURCE
/* Route the unchanged supervision protocol to an actual production worker.
 * No live backend or physical input is opened by these worker entry points. */
#include "tic80.h"
#include "tic.h"
#include "cart.h"
#include "script.h"
#ifdef TM_TEST_STUDIO
#include "tic80_mister/studio_session.h"
#include "tic80_mister/spawn_private.h"
#include "studio/studio.h"
#else
#include "tic80_mister/vm.h"
#endif
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wasm_memory_declaration_cases.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"production failed line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned spawns;
static void verify_worker(pid_t pid,const char *target)
{
    char link[64],actual[1024];snprintf(link,sizeof link,"/proc/%ld/exe",(long)pid);
    ssize_t length=readlink(link,actual,sizeof actual-1);CHECK(length>0);actual[length]=0;
    CHECK(!strcmp(actual,target));++spawns;
    printf("production worker executable verified: %s PID %ld\n",actual,(long)pid);
}
#ifdef TM_TEST_STUDIO
int __real_tm_spawn_closed(pid_t *,const tm_spawn_request *);
int __wrap_tm_spawn_closed(pid_t *pid,const tm_spawn_request *request)
{
    const char *target=getenv("TM_PRODUCTION_WORKER");
    if(!target || target[0]!='/' || !request || strcmp(request->file,"/proc/self/exe")
        || !request->argv || !request->argv[1] || strcmp(request->argv[1],"--studio-worker"))return EINVAL;
    tm_spawn_request redirected=*request;redirected.file=target;
    int result=__real_tm_spawn_closed(pid,&redirected);
    if(!result)verify_worker(*pid,target);
    return result;
}
#else
int __real_posix_spawn(pid_t *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const [],char *const []);
int __wrap_posix_spawn(pid_t *pid,const char *path,const posix_spawn_file_actions_t *actions,
    const posix_spawnattr_t *attributes,char *const argv[],char *const environment[])
{
    const char *target=getenv("TM_PRODUCTION_WORKER");
    const char *flag="--vm-worker";
    if(!target || target[0]!='/' || strcmp(path,"/proc/self/exe") || !argv || !argv[1] || strcmp(argv[1],flag))return EINVAL;
    int result=__real_posix_spawn(pid,target,actions,attributes,argv,environment);
    if(!result)verify_worker(*pid,target);
    return result;
}
#endif
static u8 *cart_bytes(const u8 *module,size_t length,size_t *size)
{
    tic_cartridge *cart=calloc(1,sizeof *cart);CHECK(cart);bool found=false;
    FOREACH_LANG(s) {if(!strcmp(s->name,"wasm")){cart->lang=s->id;found=true;}}
    CHECK(found);strcpy(cart->code.data,"// script: wasm\n");
    memcpy(cart->binary.data,module,length);cart->binary.size=length;
    u8 *bytes=malloc(sizeof *cart*2);CHECK(bytes);s32 written=tic_cart_save(cart,bytes);CHECK(written>0);
    *size=written;free(cart);return bytes;
}
int main(int argc,char **argv)
{
    (void)argv;CHECK(argc==1);setvbuf(stdout,NULL,_IONBF,0);
    size_t good_size;u8 *good=cart_bytes(declaration_import_fixed_four,sizeof declaration_import_fixed_four,&good_size);
#ifdef TM_TEST_STUDIO
    const char *folder=getenv("TM_PRODUCTION_FOLDER");CHECK(folder && folder[0]=='/');
    tm_studio_session *session=NULL;CHECK(tm_studio_session_open(&session,folder)==TM_STUDIO_OK);
    pid_t original=tm_studio_session_pid(session);CHECK(original>0);
#endif
    for(unsigned i=0;i<sizeof declaration_cases/sizeof *declaration_cases;++i) {
        size_t size;u8 *bytes=cart_bytes(declaration_cases[i].bytes,declaration_cases[i].size,&size);
#ifdef TM_TEST_STUDIO
        CHECK(tm_studio_session_load(session,bytes,size,"declaration.tic")==TM_STUDIO_OK);
        int status=tm_studio_session_run(session,2000);
        printf("%s: RUN status %d mode %d worker %ld\n",declaration_cases[i].name,status,tm_studio_session_mode(session),(long)tm_studio_session_pid(session));
        CHECK(status==TM_STUDIO_OK || (!declaration_cases[i].valid && status==TM_STUDIO_ERROR));
        CHECK((tm_studio_session_mode(session)==TIC_RUN_MODE)==declaration_cases[i].valid);
        CHECK(tm_studio_session_pid(session)==original);
        CHECK(tm_studio_session_load(session,good,good_size,"replacement.tic")==TM_STUDIO_OK);
        CHECK(tm_studio_session_run(session,2000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_mode(session)==TIC_RUN_MODE && tm_studio_session_pid(session)==original);
#else
        tm_vm *vm=NULL;CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==(declaration_cases[i].valid?TM_VM_OK:TM_VM_ERROR));tm_vm_close(vm);
        CHECK(tm_vm_open(&vm,good,good_size)==TM_VM_OK);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);tm_vm_close(vm);
#endif
        free(bytes);printf("%s: production declaration and replacement passed\n",declaration_cases[i].name);
    }
#ifdef TM_TEST_STUDIO
    CHECK(tm_studio_session_close(session)==TM_STUDIO_OK);CHECK(spawns==1);
    puts("Production Studio worker: eight supported declarations, thirteen rejected cases, twenty-one replacements and retained worker passed");
#else
    CHECK(spawns==2*(sizeof declaration_cases/sizeof *declaration_cases));
    puts("Production player worker: eight supported declarations, thirteen rejected cases and twenty-one replacements passed");
#endif
    free(good);return 0;
}
