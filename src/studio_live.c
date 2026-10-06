#define _GNU_SOURCE
#include "tic80_mister/studio_session.h"
#include "studio/studio.h"
#include "tic80_mister/backend.h"
#include "tic80_mister/hid_wheel.h"
#include "tic80_mister/input.h"
#include "tic80_mister/live_log.h"
#include "tic80_mister/memory_map.h"
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
static volatile sig_atomic_t stopping;
static tm_live_log *logs;
static void interrupt(int signal) { (void)signal; stopping=1; }
static uint64_t counter(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
static int session_lost(tm_backend *backend) {
    return tm_backend_core_selected(backend)==1 && tm_backend_cart_pending(backend)<0;
}
static int load(tm_studio_session *studio,const char *path) {
    return tm_studio_session_load_file(studio,path,5000)==TM_STUDIO_OK?0:-1;
}
int main(int argc,char **argv) {
    if(argc>=2 && !strcmp(argv[1],"--hid-wheel-worker")) return tm_hid_wheel_worker(argc,argv);
    if(argc>=2 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    const char *folder=NULL,*cart=NULL,*memory=NULL,*core_name=NULL,*saves=NULL;
    unsigned long limit=0,pulse=0; int run=0;
    tm_fft_config capture={0};
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--run")) { run=1; continue; }
        if(!strcmp(argv[i],"--fft")) { if(tm_fft_configure(&capture,NULL)) goto usage; continue; }
        if(i+1>=argc) goto usage;
        const char *option=argv[i++], *value=argv[i];
        if(!strcmp(option,"--folder")) folder=value;
        else if(!strcmp(option,"--saves")) saves=value;
        else if(!strcmp(option,"--cart")) cart=value;
        else if(!strcmp(option,"--memory")) memory=value;
        else if(!strcmp(option,"--core-name")) core_name=value;
        else if(!strcmp(option,"--fft-device")) { if(tm_fft_configure(&capture,value)) goto usage; }
        else {
            char *end; errno=0; unsigned long n=strtoul(value,&end,0);
            if(errno || !*value || *end || *value=='-') goto usage;
            if(!strcmp(option,"--ticks")) limit=n;
            else if(!strcmp(option,"--pulse") && n<=UINT32_MAX) pulse=n;
            else goto usage;
        }
    }
    if(!folder || (run&&!cart) || (core_name&&!memory)) goto usage;
    if(!memory) {
        // Keep playback and interpretation off CPU 0's USB/SD interrupts.
        // Ordinary nice priorities let publication preempt interpretation;
        // initialization, save preparation and logging use lower priorities.
        cpu_set_t cpu; CPU_ZERO(&cpu); CPU_SET(1,&cpu);
        if(sched_setaffinity(0,sizeof cpu,&cpu) || setpriority(PRIO_PROCESS,0,-20)) {
            perror("Studio playback scheduling"); return 1;
        }
    }
    signal(SIGINT,interrupt); signal(SIGTERM,interrupt);
    tm_backend backend; tm_studio_session *studio=NULL;
    if(tm_backend_open(&backend,memory)) return 1;
    if(core_name) backend.core_name_path=core_name;
    logs=tm_live_log_open(stdout);
    if(!logs) { tm_backend_close(&backend); return 1; }
    int failed=1,departed=0; unsigned long ticks=0,recoveries=0,over_budget=0;
    unsigned long completed=0,waiting=0,first_wait=0,run_waiting=0; int pending=0;
    enum { OP_TICK, OP_LOAD, OP_RUN, OP_PAUSE, OP_RESET } operation=OP_TICK;
    uint8_t *queued_cart=NULL,*loading_cart=NULL; size_t queued_size=0,loading_size=0;
    char queued_source[TICNAME_MAX]={0},loading_source[TICNAME_MAX]={0};
    unsigned long cart_requests=0,cart_loaded=0,cart_rejected=0,cart_superseded=0;
    int run_ready=0,confirmation=0;
    unsigned long cart_prompts=0,cart_cancelled=0;
    int reset_held=0,reset_resume=0;
    unsigned long reset_holds=0,reset_runs=0;
    int reconnecting=0,reload_pending=0,generation_valid=0;
    uint32_t previous_status=0; tm_core_generation generation={0};
    unsigned long reloads=0; uint64_t reload_started=0;
    const int16_t silence[3200]={0};
    uint64_t total=0,maximum=0,started=0;
    if(tm_backend_start(&backend)) goto done;
    backend.pacer.target_frames=3200;
    if(tm_studio_session_open_on_cpu(&studio,folder,saves,&capture,memory?-1:1)!=TM_STUDIO_OK) goto done;
    if(capture.enabled) { tm_live_log_printf(logs,"Studio microphone capture: %s\n",tm_fft_status_name(tm_studio_session_fft_status(studio))); }
    if(cart && load(studio,cart)) goto done;
    if(run && (tm_backend_status(&backend)&1)) reset_resume=1;
    else if(run) {
        int launch=tm_studio_session_run(studio,5000);
        if(launch==TM_STUDIO_RECOVERED) ++recoveries;
        else if(launch==TM_STUDIO_SAVE_ERROR) {
            tm_live_log_printf(logs,"Studio RUN save rejected: mode=%d; editor remains available\n",tm_studio_session_mode(studio));
        }
        else if(launch!=TM_STUDIO_OK || tm_studio_session_mode(studio)!=TIC_RUN_MODE) goto done;
    }
    // Prime only after synchronous worker initialization and CLI loading.
    // The DAC then has two silence ticks while the first request completes.
    if(tm_backend_audio(&backend,silence,1600)) goto done;
    tm_input_state input_state={0};
    previous_status=tm_backend_status(&backend);
    generation_valid=tm_backend_core_generation(&backend,&generation)==1;
    tm_live_log_printf(logs,"TIC-80 Studio live ready: mode=%d worker=%ld session=%u\n",
        tm_studio_session_mode(studio),(long)tm_studio_session_pid(studio),backend.session);
    started=counter(); failed=0;
    while(!stopping && (!limit || ticks<limit)) {
        int selected=tm_backend_core_selected(&backend);
        if(selected==0) { departed=1; break; }
        if(selected<0) { failed=1; break; }
        uint64_t begin=counter();
        int transfer=reconnecting?-1:tm_backend_cart_pending(&backend);
        if(transfer<0 && !reconnecting) {
            reconnecting=1; ++reloads;
            if(!reload_pending) reload_started=counter();
            reload_pending=1;
            tm_live_log_printf(logs,"Studio FPGA session lost; retaining last ACK\n");
        }
        uint32_t status=reconnecting?0:tm_backend_status(&backend);
        int held=reload_pending || (status&1);
        if(held && !reset_held) {
            ++reset_holds;
            reset_resume|=tm_studio_session_mode(studio)==TIC_RUN_MODE ||
                (pending && (operation==OP_RUN || operation==OP_RESET)) || run_ready;
            // A transfer was already ACKed when LOAD started. Retain its
            // private bytes if reset cancels decoding; a newer ticket wins.
            if(loading_cart) {
                if(queued_cart) { free(loading_cart); ++cart_superseded; }
                else { queued_cart=loading_cart; queued_size=loading_size; strcpy(queued_source,loading_source); }
                loading_cart=NULL; loading_size=0;
            }
            if(tm_studio_session_begin_pause(studio)!=TM_STUDIO_OK) { failed=1; break; }
            pending=1; operation=OP_PAUSE; run_ready=0; confirmation=0;
        }
        reset_held=held;
        int online=1;
        if(reconnecting) {
            if(tm_backend_restart(&backend)) online=0;
            else {
                backend.pacer.target_frames=3200;
                if(tm_backend_audio(&backend,silence,1600)) { failed=1; break; }
                reconnecting=0; transfer=tm_backend_cart_pending(&backend);
                // An old session's falling reset edge cannot qualify Main's
                // initialization of this new FPGA session.
                previous_status=status=tm_backend_status(&backend);
                input_state=(tm_input_state){0}; backend.inputs=(tm_input_snapshot){0};
                tm_live_log_printf(logs,"Studio FPGA reconnected: session=%u\n",backend.session);
            }
        }
        if(reload_pending) {
            if(counter()-reload_started>10000000000ULL) {
                fprintf(stderr,"Studio MiSTer initialization did not complete\n"); failed=1; break;
            }
            if(online) {
                tm_core_generation next={0}; int observed=tm_backend_core_generation(&backend,&next);
                if(observed<0) { failed=1; break; }
                if(!(status&1) && ((previous_status&1) ||
                    (observed==1 && generation_valid && memcmp(&generation,&next,sizeof next)))) {
                    reload_pending=0;
                    tm_live_log_printf(logs,"Studio MiSTer initialization ready\n");
                }
            }
        }
        if(online) {
            previous_status=status;
            reset_held=reload_pending || (status&1);
            if(!reload_pending) generation_valid=tm_backend_core_generation(&backend,&generation)==1;
        }
        if(online && transfer>0) {
            uint8_t *bytes=NULL; size_t size=0;
            char source[TICNAME_MAX];
            _Static_assert(TM_CART_SOURCE_CAPACITY==TICNAME_MAX,"Source path capacity changed");
            transfer=tm_backend_cart_source(&backend,&bytes,&size,source);
            if(transfer<0) { if(session_lost(&backend)) continue; failed=1; break; }
            if(transfer) ++cart_requests;
            if(transfer==1) {
                if(queued_cart) { free(queued_cart); ++cart_superseded; }
                queued_cart=bytes; queued_size=size; strcpy(queued_source,source);
            } else if(transfer==2) {
                ++cart_rejected; tm_live_log_printf(logs,"MiSTer cartridge transfer rejected; current cartridge kept\n");
            }
        }
        // Pace at four ticks, retaining 25 ms for checkpoint copying and PCM
        // publication. The remaining credit lets an ordinary worker finish;
        // a hung worker cannot consume the copy reserve through a long wait.
        int budget=pending && online?tm_backend_work_budget(&backend,1200,40):0;
        if(budget<0) { if(session_lost(&backend)) continue; failed=1; break; }
        int result=pending?tm_studio_session_poll(studio,(unsigned)budget):TM_STUDIO_OK;
        if(result==TM_STUDIO_EXIT) break;
        if(result==TM_STUDIO_RECOVERED) {
            if(operation!=OP_PAUSE) ++recoveries;
            pending=0; run_ready=0;
            // A dialog is recreated after recovery from the retained candidate,
            // with NO selected again. Approval is never inferred from a crash.
            if(confirmation && loading_cart) {
                if(queued_cart) { free(loading_cart); ++cart_superseded; }
                else { queued_cart=loading_cart; queued_size=loading_size; strcpy(queued_source,loading_source); }
                loading_cart=NULL; loading_size=0;
            }
            confirmation=0;
            free(loading_cart); loading_cart=NULL; loading_size=0;
            tm_live_log_printf(logs,"Studio worker %s: worker=%ld mode=%d\n",operation==OP_PAUSE?"paused":"recovered",(long)tm_studio_session_pid(studio),tm_studio_session_mode(studio));
        }
        else if(result==TM_STUDIO_OK) {
            if(pending && operation==OP_TICK) ++completed;
            else if(pending && operation==OP_LOAD) {
                free(loading_cart); loading_cart=NULL; loading_size=0; run_ready=1;
            }
            else if(pending && operation==OP_RUN) {
                ++cart_loaded; tm_live_log_printf(logs,"MiSTer cartridge running: worker=%ld\n",(long)tm_studio_session_pid(studio));
            }
            else if(pending && operation==OP_RESET) ++reset_runs;
            pending=0;
        }
        else if(result==TM_STUDIO_CONFIRM) {
            pending=0; confirmation=1; ++cart_prompts;
            tm_live_log_printf(logs,"MiSTer cartridge waiting for unsaved-changes confirmation\n");
        }
        else if(result==TM_STUDIO_CART_SELECTED) {
            tm_live_log_printf(logs,"MiSTer cartridge selected: %s\n",tm_studio_session_name(studio)->path);
            free(loading_cart); loading_cart=NULL; loading_size=0;
            pending=0; confirmation=0; run_ready=1;
        }
        else if(result==TM_STUDIO_CANCELLED) {
            free(loading_cart); loading_cart=NULL; loading_size=0;
            pending=0; confirmation=0; run_ready=0; ++cart_cancelled;
            tm_live_log_printf(logs,"MiSTer cartridge selection cancelled; edits kept\n");
        }
        else if(result==TM_STUDIO_SAVE_ERROR) {
            if(pending && operation==OP_TICK) ++completed; pending=0; run_ready=0;
            tm_live_log_printf(logs,"Studio RUN save rejected: mode=%d; editor remains available\n",tm_studio_session_mode(studio));
        }
        else if(result==TM_STUDIO_CART_ERROR) {
            ++cart_rejected; pending=0; run_ready=0; confirmation=0;
            free(loading_cart); loading_cart=NULL; loading_size=0;
            tm_live_log_printf(logs,"MiSTer cartridge rejected; previous cartridge kept\n");
        }
        else if(result==TM_STUDIO_PENDING) {
            if(!waiting) first_wait=ticks; ++waiting;
            if(!reset_held && operation==OP_TICK && tm_studio_session_mode(studio)==TIC_RUN_MODE) ++run_waiting;
        }
        else { failed=1; break; }
        if(!online) {
            // The FPGA cannot consume PCM while absent. Keep cancellation
            // responsive without touching payload/control words or spinning.
            struct timespec delay={0,1000000}; nanosleep(&delay,NULL); continue;
        }
        // The acknowledged Studio frame already has the required packed
        // RGBA8888 layout. Transport keeps its own copy if scanout is busy.
        // A late/hung tick cannot stop the DAC. Preserve its last completed
        // picture and submit silence while its replacement starts; never
        // replay an old music block or copy a partially published product.
        if(tm_backend_audio(&backend,(reset_held || result==TM_STUDIO_PENDING || result==TM_STUDIO_SAVE_ERROR || operation==OP_LOAD || operation==OP_PAUSE)?silence:tm_studio_session_audio(studio),800) ||
           tm_backend_present_realtime(&backend,(const uint8_t*)tm_studio_session_screen(studio),TM_FRAME_BYTES)) {
            if(session_lost(&backend)) continue; failed=1; break;
        }
        ++ticks;
        if(!reset_held && !pending && (!limit || ticks<limit)) {
            if(queued_cart && !confirmation) {
                if(run_ready) ++cart_superseded;
                int accepted=tm_studio_session_begin_select_source(studio,queued_cart,queued_size,"MiSTer cart.tic",queued_source,5000);
                if(accepted!=TM_STUDIO_OK) { failed=1; break; }
                loading_cart=queued_cart; loading_size=queued_size; strcpy(loading_source,queued_source);
                queued_cart=NULL; queued_size=0; queued_source[0]=0; run_ready=0; reset_resume=0;
                pending=1; operation=OP_LOAD;
            } else if(run_ready) {
                if(tm_studio_session_begin_run(studio,5000)!=TM_STUDIO_OK) { failed=1; break; }
                run_ready=0; pending=1; operation=OP_RUN;
            } else if(reset_resume) {
                if(tm_studio_session_begin_run(studio,5000)!=TM_STUDIO_OK) { failed=1; break; }
                reset_resume=0; pending=1; operation=OP_RESET;
            } else {
                tm_input_snapshot snapshot; tic80_input input={0};
                input.gamepads.data=tm_backend_gamepads(&backend);
                if(tm_backend_inputs(&backend,&snapshot)<0) { if(session_lost(&backend)) continue; failed=1; break; }
                tm_input_convert(&input_state,&snapshot,&input);
                if(ticks==1) input.gamepads.data|=(uint32_t)pulse;
                unsigned timeout=tm_studio_session_mode(studio)==TIC_RUN_MODE?250:5000;
                if(tm_studio_session_begin_tick(studio,input,UINT64_MAX,timeout)!=TM_STUDIO_OK) { failed=1; break; }
                pending=1; operation=OP_TICK;
            }
        }
        uint64_t duration=counter()-begin; total+=duration;
        if(duration>maximum) maximum=duration;
        over_budget+=duration>16666667;
        if(tm_backend_pace(&backend,&stopping)) {
            if(!stopping && session_lost(&backend)) continue;
            if(!stopping) failed=1; break;
        }
    }
    if(!failed && !stopping && !departed && tm_backend_drain(&backend)) failed=1;
done:
    free(queued_cart); free(loading_cart);
    if(failed && tm_backend_core_selected(&backend)==0) { departed=1; failed=0; }
    if(tm_studio_session_close(studio)!=TM_STUDIO_OK) failed=1;
    tm_backend_close(&backend);
    tm_live_log_printf(logs,"Studio live stopped: ticks=%lu recoveries=%lu mean_ms=%.3f max_ms=%.3f over_budget=%lu wall_seconds=%.3f departed=%d error=%d\n",
        ticks,recoveries,ticks?total/(ticks*1000000.0):0,maximum/1e6,over_budget,
        started?(counter()-started)/1e9:0,departed,failed);
    tm_live_log_printf(logs,"Studio async: completed_ticks=%lu waiting_frames=%lu first_waiting_frame=%lu\n",completed,waiting,first_wait);
    tm_live_log_printf(logs,"Studio RUN: waiting_frames=%lu\n",run_waiting);
    tm_live_log_printf(logs,"Studio OSD: requests=%lu loaded=%lu rejected=%lu superseded=%lu\n",cart_requests,cart_loaded,cart_rejected,cart_superseded);
    tm_live_log_printf(logs,"Studio selection: prompts=%lu cancelled=%lu\n",cart_prompts,cart_cancelled);
    tm_live_log_printf(logs,"Studio reset: holds=%lu runs=%lu\n",reset_holds,reset_runs);
    tm_live_log_printf(logs,"Studio FPGA: reloads=%lu\n",reloads);
    unsigned dropped=tm_live_log_close(logs); logs=NULL;
    if(dropped) fprintf(stderr,"Studio log: dropped=%u\n",dropped);
    return failed?1:0;
usage:
    fprintf(stderr,"Usage: %s --folder directory [--saves existing_directory] [--cart file.tic|file.png --run] [--fft | --fft-device name] [--ticks N] [--pulse buttons] [--memory fixture --core-name fixture]\n",argv[0]);
    return 2;
}
