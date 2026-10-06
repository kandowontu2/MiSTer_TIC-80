#define _GNU_SOURCE
#include "tic80_mister/vm.h"
#include "tic80_mister/pmem.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include "tools.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static unsigned direct_errors;
static u64 ticks;
static void error(const char *s) { fprintf(stderr,"direct: %s\n",s); ++direct_errors; }
static u64 counter(void *data) { (void)data; return ticks; }
static u64 frequency(void *data) { (void)data; return 60; }
static uint64_t nanoseconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec; }
static uint64_t milliseconds(void) { return nanoseconds()/1000000; }
static u32 value(tm_vm *vm,int slot) { return tic_api_pmem((tic_mem *)tm_vm_product(vm),slot,0,false); }
static const tic_script *language(const char *name) { FOREACH_LANG(s) if (!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
static tm_vm *source(const char *name,const char *code)
{
    static tic_cartridge cart;
    memset(&cart,0,sizeof cart); cart.lang=language(name)->id;
    strcpy(cart.code.data,code);
    if (!strcmp(name,"wasm")) {
        /* TIC calls imported env.btn(0); its true branch loops forever. */
        static const u8 binary[]={0,97,115,109,1,0,0,0,1,9,2,96,0,0,96,1,127,1,127,
            2,11,1,3,101,110,118,3,98,116,110,0,1,3,2,1,0,5,3,1,0,4,
            7,7,1,3,84,73,67,0,1,10,16,1,14,0,65,0,16,0,4,64,3,64,12,0,11,11,11};
        memcpy(cart.binary.data,binary,sizeof binary); cart.binary.size=sizeof binary;
    }
    u8 *bytes=malloc(sizeof cart*2); CHECK(bytes);
    s32 size=tic_cart_save(&cart,bytes);
    tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK && vm);
    free(bytes); return vm;
}
static pid_t only_child(void)
{
    /* MiSTer's kernel omits /proc/PID/task/PID/children. /proc/PID/stat
     * provides PPID without requiring CONFIG_CHECKPOINT_RESTORE. */
    DIR *directory=opendir("/proc"); CHECK(directory);
    struct dirent *entry; pid_t child=0;
    while ((entry=readdir(directory))) {
        char *end; long pid=strtol(entry->d_name,&end,10); if (*end || pid<=0) continue;
        char path[96],text[1024]; snprintf(path,sizeof path,"/proc/%ld/stat",pid);
        FILE *file=fopen(path,"r"); if (!file) continue;
        char *line=fgets(text,sizeof text,file); fclose(file); if (!line) continue;
        char *fields=strrchr(text,')'); char state; long parent;
        if (fields && sscanf(fields+1," %c %ld",&state,&parent)==2 && parent==getpid()) {
            CHECK(!child); child=(pid_t)pid;
        }
    }
    closedir(directory); CHECK(child>0); return child;
}
static int descriptors(void)
{
    DIR *directory=opendir("/proc/self/fd"); CHECK(directory); int count=0;
    struct dirent *entry; while ((entry=readdir(directory))) if (entry->d_name[0]!='.') ++count;
    closedir(directory); return count;
}
static void demos(void)
{
    const char *names[]={"lua"
#if TM_WORKER_EXTENDED
        ,"js","moon","yue","fennel","scheme","squirrel","python","wren","janet","wasm","ruby","miniscript","forth"
#endif
    };
    u8 *bytes=malloc(sizeof(tic_cartridge)); CHECK(bytes);
    for (unsigned n=0;n<sizeof names/sizeof *names;++n) {
        const tic_script *s=language(names[n]);
        s32 size=tic_tool_unzip(bytes,sizeof(tic_cartridge),s->demo.data,s->demo.size); CHECK(size>0);
        tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK);
        tic80 *direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct);
        direct->callback.error=error; tic80_load(direct,bytes,size);
        char key[33]; tm_pmem_key(direct,key); CHECK(!strcmp(key,tm_vm_key(vm)));
        uint64_t first_ns=0,steady_ns=0,max_ns=0;
        for (ticks=0;ticks<120;++ticks) {
            tic80_input input={0}; if (ticks==1) input.gamepads.data=8;
            tic80_tick(direct,input,counter,frequency); tic80_sound(direct); CHECK(!direct_errors);
            uint64_t started=nanoseconds(); int result=tm_vm_tick(vm,input,0);
            uint64_t duration=nanoseconds()-started;
            if (!ticks) first_ns=duration;
            else { steady_ns+=duration; if (duration>max_ns) max_ns=duration; }
            if (result!=TM_VM_OK) fprintf(stderr,"%s tick %llu returned %d after %.3f ms\n",names[n],(unsigned long long)ticks,result,duration/1e6);
            CHECK(result==TM_VM_OK);
            tic80 *mirror=tm_vm_product(vm);
            CHECK(mirror->samples.count==direct->samples.count);
            CHECK(!memcmp(direct->screen,mirror->screen,TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4));
            CHECK(!memcmp(direct->samples.buffer,mirror->samples.buffer,1600*sizeof(s16)));
            CHECK(!memcmp(((tic_mem *)direct)->ram->persistent.data,((tic_mem *)mirror)->ram->persistent.data,1024));
        }
        tic80_delete(direct); tm_vm_close(vm);
        printf("%s: supervised frame/audio/pmem/save-key parity passed; first_ms=%.3f steady_ms=%.3f max_steady_ms=%.3f\n",names[n],first_ns/1e6,steady_ns/119e6,max_ns/1e6);
    }
    free(bytes);
}
static const char *good_code="-- script: lua\n-- saveid: worker-fixture\nn=0\nfunction BOOT() pmem(0,pmem(0)+1) end\nfunction TIC() n=n+1; pmem(1,n); cls(n%16) end\n";
static void failures(void)
{
    tm_vm *good=source("lua",good_code);
    tic_api_pmem((tic_mem *)tm_vm_product(good),0,41,true);
    CHECK(tm_vm_tick(good,(tic80_input){0},0)==TM_VM_OK && value(good,0)==42 && value(good,1)==1);
    const char *phases[]={"while true do end\nfunction TIC() end", "function BOOT() while true do end end\nfunction TIC() end",
        "function TIC() while true do end end", "function TIC() end\nfunction SCN(row) if btn(0) then while true do end end end",
        "function TIC() end\nfunction BDR(row) if btn(0) then while true do end end end"};
    for (unsigned i=0;i<sizeof phases/sizeof *phases;++i) {
        tm_vm *bad=source("lua",phases[i]);
        if (i>=3) CHECK(tm_vm_tick(bad,(tic80_input){0},0)==TM_VM_OK);
        uint64_t start=milliseconds(); tic80_input trigger={0}; trigger.gamepads.data=1;
        CHECK(tm_vm_tick(bad,trigger,0)==TM_VM_TIMEOUT);
        CHECK(milliseconds()-start>=(i<3?4500:900) && milliseconds()-start<(i<3?6500:2500));
        tm_vm_close(bad);
        CHECK(tm_vm_tick(good,(tic80_input){0},0)==TM_VM_OK && value(good,1)==i+2);
    }
    tm_vm *later=source("lua","n=0\nfunction TIC() n=n+1; pmem(0,n); cls(n); if n==3 then pmem(0,9999); while true do end end end");
    CHECK(tm_vm_tick(later,(tic80_input){0},0)==TM_VM_OK);
    CHECK(tm_vm_tick(later,(tic80_input){0},0)==TM_VM_OK);
    u32 frame[TIC80_FULLWIDTH*TIC80_FULLHEIGHT]; memcpy(frame,tm_vm_product(later)->screen,sizeof frame);
    CHECK(tm_vm_tick(later,(tic80_input){0},0)==TM_VM_TIMEOUT);
    CHECK(value(later,0)==2 && !memcmp(frame,tm_vm_product(later)->screen,sizeof frame));
    tm_vm_close(later); tm_vm_close(good);
    puts("Initialization/BOOT/TIC/SCN/BDR timeouts preserve old VM and last complete snapshot");
    const struct { const char *name,*code; } runaway[]={
        {"lua","function TIC() if btn(0) then while true do end end end"},
#if TM_WORKER_EXTENDED
        {"js","function TIC(){if(btn(0)){while(true){}}}"},
        {"moon","export TIC\nTIC = ->\n if btn 0\n  while true\n   x = 1\n"},
        {"yue","global TIC = ->\n if btn 0\n  while true\n   x = 1\n"},
        {"fennel","(fn _G.TIC [] (when (btn 0) (while true nil)))"},
        {"scheme","(define (TIC) (if (t80::btn 0) (do () (#f) #f)))"},
        {"squirrel","function TIC(){if(btn(0)){while(true){}}}"},
        {"python","def TIC():\n if btn(0):\n  while True:\n   pass\n"},
        {"wren","class Game is TIC {\n construct new() {}\n TIC() {\n if(TIC.btn(0)) {\n while(true) {\n }\n }\n }\n}\n"},
        {"janet","(import tic80)\n(defn TIC [] (when (tic80/btn 0) (while true nil)))\n"},
        {"wasm","-- script: wasm\n"},
        {"ruby","def TIC\n if btn(0)\n  while true\n  end\n end\nend\n"},
        {"miniscript","TIC=function\nend function\nSCN=function(row)\n if tic80.btn(0) then\n  while 1\n  end while\n end if\nend function\n"},
        {"forth",": TIC 0 BTN IF BEGIN AGAIN THEN ;\n"},
#endif
    };
    for (unsigned i=0;i<sizeof runaway/sizeof *runaway;++i) {
        tm_vm *bad=source(runaway[i].name,runaway[i].code);
        int warm=tm_vm_tick(bad,(tic80_input){0},0);
        if (warm!=TM_VM_OK) fprintf(stderr,"%s warm tick returned %d\n",runaway[i].name,warm);
        CHECK(warm==TM_VM_OK);
        tic80_input trigger={0}; trigger.gamepads.data=1;
        int result=tm_vm_tick(bad,trigger,0);
        if (result!=TM_VM_TIMEOUT) fprintf(stderr,"%s runaway returned %d\n",runaway[i].name,result);
        CHECK(result==TM_VM_TIMEOUT); tm_vm_close(bad);
        printf("%s: runaway callback timeout passed\n",runaway[i].name);
    }
    tm_vm *syntax=source("lua","@] invalid source !!!"); CHECK(tm_vm_tick(syntax,(tic80_input){0},0)==TM_VM_ERROR); tm_vm_close(syntax);
    tm_vm *exiting=source("lua","function TIC() pmem(0,77); exit() end"); CHECK(tm_vm_tick(exiting,(tic80_input){0},0)==TM_VM_EXIT && value(exiting,0)==77); tm_vm_close(exiting);
    tm_vm *crash=source("lua",good_code); CHECK(tm_vm_tick(crash,(tic80_input){0},0)==TM_VM_OK);
    pid_t pid=only_child(); CHECK(!kill(pid,SIGSEGV));
    /* kill() queues a signal; it does not wait for the recipient to exit.
     * QEMU can otherwise finish another tick before delivering the guest
     * signal. Observe termination without reaping, so tm_vm still owns the
     * waitable child and must contain the death itself. */
    uint64_t death_start=milliseconds(); siginfo_t death;
    for (;;) {
        memset(&death,0,sizeof death);
        int observed;
        do observed=waitid(P_PID,(id_t)pid,&death,WEXITED|WNOWAIT|WNOHANG);
        while (observed<0 && errno==EINTR);
        CHECK(!observed);
        if (death.si_pid==pid) break;
        CHECK(milliseconds()-death_start<2000); usleep(1000);
    }
    int expected_death=(death.si_code==CLD_KILLED || death.si_code==CLD_DUMPED) && death.si_status==SIGSEGV;
#ifdef __SANITIZE_ADDRESS__
    expected_death|=death.si_code==CLD_EXITED && death.si_status==1;
#endif
    CHECK(expected_death);
    CHECK(tm_vm_tick(crash,(tic80_input){0},0)==TM_VM_DIED && value(crash,1)==1); tm_vm_close(crash);
    puts("Syntax errors, explicit exits and child crashes are contained");
}
static void raster(void)
{
#if TM_WORKER_EXTENDED
    const char *code="TIC=function\n tic80.pmem 0, tic80.pmem(0)+1\nend function\n"
        "SCN=function(row)\n tic80.pmem 1, tic80.pmem(1)+1\nend function\n"
        "BDR=function(row)\n tic80.pmem 2, tic80.pmem(2)+1\nend function\n"
        "MENU=function(index)\n tic80.pmem 3, index+1\nend function\n";
    tm_vm *vm=source("miniscript",code);
    for (unsigned n=1;n<=3;++n) {
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
        CHECK(value(vm,0)==n && value(vm,1)==136*n && value(vm,2)==144*n);
    }
    tm_vm_close(vm);
    /* Exercise the same callback path directly, including MENU, and check
     * that a yielded main harness continues after synchronous callbacks. */
    static tic_cartridge cart; cart.lang=language("miniscript")->id; strcpy(cart.code.data,code);
    u8 *bytes=malloc(sizeof cart*2); CHECK(bytes); s32 size=tic_cart_save(&cart,bytes);
    tic80 *direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct);
    direct->callback.error=error; tic80_load(direct,bytes,size); free(bytes);
    for (unsigned n=1;n<=3;++n) {
        tic80_tick(direct,(tic80_input){0},counter,frequency);
        CHECK(!direct_errors && tic_api_pmem((tic_mem *)direct,0,0,false)==n);
        CHECK(tic_api_pmem((tic_mem *)direct,1,0,false)==136*n);
        CHECK(tic_api_pmem((tic_mem *)direct,2,0,false)==144*n);
        language("miniscript")->callback.menu((tic_mem *)direct,n,NULL);
        CHECK(tic_api_pmem((tic_mem *)direct,3,0,false)==n+1);
    }
    tic80_delete(direct);
    puts("MiniScript SCN/BDR/MENU run synchronously and preserve the yielded main harness");
#endif
}
static void miniscript_keys(void)
{
#if TM_WORKER_EXTENDED
    /* Keyboard and controller states deliberately disagree. A self-comparison
     * against the same binding would miss routing keyp() through btnp(). */
    char code[1400];
    snprintf(code,sizeof code,
        "TIC=function\n"
        " tic80.pmem 0, tic80.keyp()\n"
        " tic80.pmem 1, tic80.keyp(%d)\n"
        " tic80.pmem 2, tic80.keyp(%d)\n"
        " tic80.pmem 3, tic80.keyp(%d,3,2)\n"
        " tic80.pmem 4, tic80.btnp(1)\n"
        " tic80.pmem 5, tic80.key(%d)\n"
        " tic80.pmem 6, tic80.keyp(%d,3,2)\n"
        " tic80.pmem 7, tic80.keyp(%d)\n"
        "end function\n",
        tic_key_a,tic_key_f12,tic_key_f12,tic_key_a,tic_key_a,tic_key_numpadperiod);
    tm_vm *vm=source("miniscript",code);
    static tic_cartridge cart;
    memset(&cart,0,sizeof cart); cart.lang=language("miniscript")->id;
    strcpy(cart.code.data,code);
    u8 *bytes=malloc(sizeof cart*2); CHECK(bytes);
    s32 size=tic_cart_save(&cart,bytes);
    tic80 *direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct);
    direct->callback.error=error; tic80_load(direct,bytes,size); free(bytes);
    for (unsigned frame=0;frame<14;++frame) {
        tic80_input input={0};
        if (frame==1 || frame==2) input.gamepads.data=2;
        int held=(frame>=2 && frame<=10) || frame==12;
        if (held) {
            input.keyboard.keys[0]=tic_key_a;
            input.keyboard.keys[1]=tic_key_f12;
            input.keyboard.keys[2]=tic_key_numpadperiod;
        }
        tic80_tick(direct,input,counter,frequency); CHECK(!direct_errors);
        CHECK(tm_vm_tick(vm,input,0)==TM_VM_OK);
        int edge=frame==2 || frame==12;
        int repeat=edge || frame==6 || frame==8 || frame==10;
        const u32 expected[]={edge,edge,edge,repeat,frame==1,held,repeat,edge};
        for (unsigned slot=0;slot<sizeof expected/sizeof *expected;++slot) {
            u32 a=tic_api_pmem((tic_mem *)direct,slot,0,false),b=value(vm,slot);
            if (a!=expected[slot] || b!=expected[slot])
                fprintf(stderr,"MiniScript frame=%u slot=%u expected=%u direct=%u worker=%u\n",
                        frame,slot,expected[slot],a,b);
            CHECK(a==expected[slot] && b==expected[slot]);
        }
    }
    tic80_delete(direct); tm_vm_close(vm);
    puts("MiniScript keyp: controller independence, any/specific keys, high keycodes, repeat and release passed");
#endif
}
static void sound_input(void)
{
    static tic_cartridge cart;
    char code[1500];
    snprintf(code,sizeof code,
        "-- script: lua\nn=0\nfunction BOOT() local x,y,l,m,r,sx,sy=mouse(); "
        "pmem(2,x); pmem(3,y); pmem(4,(l and 1 or 0)+(m and 2 or 0)+(r and 4 or 0)); "
        "pmem(5,sx+32); pmem(6,sy+32) end\n"
        "function TIC() n=n+1; cls(n%%2); pmem(0,n); local b=0; "
        "for i=0,31 do if btn(i) then b=b|(1<<i) end end; pmem(7,b); "
        "pmem(8,(key(%d) and 1 or 0)+(key(%d) and 2 or 0)+(key(%d) and 4 or 0)+(key(%d) and 8 or 0)); "
        "if n==1 then sfx(0,'C-4',-1,0,15,0) end end\n",
        tic_key_a,tic_key_ctrl,tic_key_shift,tic_key_f12);
    strcpy(cart.code.data,code);
    cart.bank0.palette.vbank0.colors[1]=(tic_rgb){255,0,0};
    memset(cart.bank0.sfx.waveforms.items[0].data,0xf0,sizeof cart.bank0.sfx.waveforms.items[0].data);
    u8 *bytes=malloc(sizeof cart*2); CHECK(bytes); s32 size=tic_cart_save(&cart,bytes);
    tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK);
    tic80 *direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct);
    direct->callback.error=error; tic80_load(direct,bytes,size); free(bytes);
    tic80_input input={0}; input.gamepads.data=0x80402010;
    input.mouse.x=99+TIC80_MARGIN_LEFT; input.mouse.y=70+TIC80_MARGIN_TOP;
    input.mouse.left=input.mouse.middle=1; input.mouse.scrollx=-17; input.mouse.scrolly=19;
    input.keyboard.keys[0]=tic_key_a; input.keyboard.keys[1]=tic_key_ctrl;
    input.keyboard.keys[2]=tic_key_shift; input.keyboard.keys[3]=tic_key_f12;
    int audible=0;
    for (ticks=0;ticks<120;++ticks) {
        if (ticks==30) memset(&input,0,sizeof input);
        tic80_tick(direct,input,counter,frequency); tic80_sound(direct); CHECK(!direct_errors);
        CHECK(tm_vm_tick(vm,input,0)==TM_VM_OK);
        CHECK(!memcmp(direct->samples.buffer,tm_vm_product(vm)->samples.buffer,1600*sizeof(s16)));
        CHECK(!memcmp(direct->screen,tm_vm_product(vm)->screen,TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4));
        for (int i=0;i<1600;++i) if (tm_vm_product(vm)->samples.buffer[i]) audible=1;
        CHECK(value(vm,7)==input.gamepads.data && value(vm,8)==(ticks<30?15:0));
    }
    CHECK(audible && value(vm,0)==120 && value(vm,2)==99 && value(vm,3)==70);
    CHECK(value(vm,4)==3 && value(vm,5)==15 && value(vm,6)==51);
    tic80_delete(direct); tm_vm_close(vm);
    puts("Nonzero stereo PCM, full four-pad mask, four keys and BOOT mouse/wheel survive IPC and release");
}
extern char **environ;
static void orphan(void)
{
    CHECK(!prctl(PR_SET_CHILD_SUBREAPER,1));
    int pipefds[2]; CHECK(!pipe2(pipefds,O_CLOEXEC));
    int sourcefd=fcntl(pipefds[1],F_DUPFD_CLOEXEC,10); CHECK(sourcefd>=0);
    posix_spawn_file_actions_t actions; CHECK(!posix_spawn_file_actions_init(&actions));
    CHECK(!posix_spawn_file_actions_adddup2(&actions,sourcefd,3));
    char *arguments[]={"vm-test-probe","--orphan-probe",NULL}; pid_t parent;
    CHECK(!posix_spawn(&parent,"/proc/self/exe",&actions,NULL,arguments,environ));
    posix_spawn_file_actions_destroy(&actions); close(sourcefd); close(pipefds[1]);
    pid_t child; CHECK(read(pipefds[0],&child,sizeof child)==sizeof child); close(pipefds[0]);
    usleep(50000); /* Let the probe enter its runaway tick before stopping it. */
    CHECK(!kill(parent,SIGKILL)); CHECK(waitpid(parent,NULL,0)==parent);
    uint64_t start=milliseconds(); int status; pid_t waited;
    while ((waited=waitpid(child,&status,WNOHANG))==0) { CHECK(milliseconds()-start<2000); usleep(1000); }
    CHECK(waited==child);
    CHECK((WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL) || (WIFEXITED(status) && WEXITSTATUS(status)==0));
    puts("Abrupt parent death kills/reaps its execution process");
}
int main(int argc,char **argv)
{
    if (argc>=2 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    if (argc==2 && !strcmp(argv[1],"--io-test")) { sound_input(); return 0; }
    if (argc==2 && !strcmp(argv[1],"--keyboard-test")) { miniscript_keys(); return 0; }
    if (argc==2 && !strcmp(argv[1],"--orphan-probe")) {
        tm_vm *vm=source("lua","function TIC() while true do end end"); pid_t child=only_child();
        CHECK(write(3,&child,sizeof child)==sizeof child);
        tm_vm_tick(vm,(tic80_input){0},0); for (;;) pause();
        tm_vm_close(vm);
    }
    CHECK(argc==1); setvbuf(stdout,NULL,_IONBF,0);
    int baseline=descriptors(); demos(); raster(); miniscript_keys(); sound_input(); failures(); orphan();
    CHECK(descriptors()==baseline);
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    puts("Supervised runtime lifecycle passed without descriptor or child leaks");
    return 0;
}
