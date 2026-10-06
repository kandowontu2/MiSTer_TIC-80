#define _GNU_SOURCE
#include "tic80_mister/studio_session.h"
#include "tic80_mister/memory_equal.h"
#include "tic80_mister/cart_guard_private.h"
#include "tic80_mister/pmem.h"
#include "tic80_mister/history_private.h"
#include "studio/studio.h"
#include "studio/config.h"
#include "ext/md5.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include "tic80_mister/spawn_private.h"
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static tm_cart_guard *worker_guard;
#define MAGIC 0x53544943u
#define PAGE_BYTES 4096u
#define PAGES ((sizeof(tic_cartridge)+PAGE_BYTES-1)/PAGE_BYTES)
#define CLIP_BYTES (1024u*1024u+1)
#define CART_BYTES (4u*1024u*1024u)
typedef struct {
    CartName name;
    char cwd[TICNAME_MAX];
    u8 saved_hash[16];
    typeof(((StudioConfig*)0)->options) options;
    tm_studio_banks banks;
    tm_studio_sprite_view sprites[8];
    tm_studio_code_view code;
    u32 persistent[256];
    char pmem_key[33];
    int mode, home;
    bool modified, caps_lock, caps_down, selecting;
} state;
typedef struct {
    _Atomic u32 ready;
    u32 request;
    CartName previous, destination;
    char target[PATH_MAX];
    uint64_t device, inode, size;
    u8 cart_hash[16], file_hash[16];
} save_publication;
typedef struct {
    _Atomic u32 ready;
    u32 request;
    char path[PATH_MAX];
    uint64_t device, inode;
} save_temporary;
typedef struct {
    u32 magic, sequence, acknowledged, sample_count, clip_size, cart_size;
    int result, restore, restore_save_failure, save_warning;
    int history_changed;
    tm_fft_config capture;
    int capture_status, worker_cpu;
    u32 recovery_request;
    save_publication publication;
    save_temporary temporary;
    char folder[PATH_MAX], save_folder[PATH_MAX], load_name[TICNAME_MAX], load_source[TICNAME_MAX];
    tic80_input input;
    uint64_t clock_ns;
    state data;
    u8 dirty[PAGES];
    tic_cartridge cart;
    u32 screen[TIC80_FULLWIDTH*TIC80_FULLHEIGHT];
    s16 audio[1600];
    char clipboard[CLIP_BYTES];
    u8 load[CART_BYTES];
} shared;
struct tm_studio_session {
    _Atomic pid_t pid;
    int socket, memory;
    int history_fd, pending_history_fd;
    shared *ipc;
    tm_fft_config capture;
    int capture_status;
    state data;
    tic_cartridge cart;
    u32 screen[TIC80_FULLWIDTH*TIC80_FULLHEIGHT];
    s16 audio[1600];
    char clipboard[CLIP_BYTES];
    u32 clip_size, sequence;
    bool suppress_input;
    int pending; /* 1: tick, 2: replacement worker startup */
    uint64_t deadline;
    pthread_t starter;
    pthread_mutex_t startup_lock;
    pthread_cond_t startup_condition;
    bool starter_created, start_job, stop_starter;
    _Atomic bool startup_done;
    int startup_result;
    char save_folder[PATH_MAX], save_key[33];
    tm_pmem save;
    u32 save_values[256];
    unsigned save_ticks;
    bool save_write_error, save_read_error, save_failed, save_rejected, save_retrying;
    pthread_t save_preparer;
    pthread_mutex_t save_lock;
    pthread_cond_t save_condition;
    bool save_preparer_created, save_job, stop_save_preparer, save_transition, cancel_after_save;
    _Atomic bool save_prepared;
    int save_prepare_result;
    char save_target[33];
};
static char worker_save_key[33];
static bool worker_capture_pmem;
static bool worker_save_rejected;
static bool worker_selecting;
static int worker_selection_result;
static uint64_t worker_history_revision;
static void selection_confirmed(Studio *studio,bool yes,void *argument) {
    shared *m=argument;
    worker_selecting=false;
    if(!yes) { worker_selection_result=TM_STUDIO_CANCELLED; return; }
    char source[TICNAME_MAX];
    bool verified=*m->load_source && tm_studio_cart_source(m->load_source,m->load,m->cart_size,source);
    if(tm_studio_hashload_apply(studio,m->load,m->cart_size,m->load_name,NULL)) {
        if(verified) studioSetCartName(studio,strrchr(source,'/')+1,source);
        else {
            studioCart(studio)->path[0]=0;
            tm_studio_popup(studio,"Save creates a working copy");
        }
        setStudioMode(studio,m->data.home);
        worker_selection_result=TM_STUDIO_CART_SELECTED;
    } else worker_selection_result=TM_STUDIO_CART_ERROR;
}
static bool save_key_valid(const char key[33]) {
    size_t size=strnlen(key,33);
    if(!size) return true;
    if(size!=32) return false;
    for(unsigned i=0;i<32;++i)
        if(!((key[i]>='0' && key[i]<='9') || (key[i]>='a' && key[i]<='f'))) return false;
    return true;
}
static int worker_save_load(Studio *studio,void *argument) {
    worker_save_rejected=true;
    shared *m=argument; tic_mem *tic=getMemory(studio);
    char key[33],path[PATH_MAX]; tm_pmem_key((tic80*)tic,key);
    u32 initial[256]={0};
    if(!strcmp(key,m->data.pmem_key)) {
        // Restart from the last ACK, even if its background write is pending.
        memcpy(initial,m->data.persistent,sizeof initial);
    } else {
        if(snprintf(path,sizeof path,"%s/%s.pmem",m->save_folder,key)>=(int)sizeof path) return -1;
        tm_pmem load={0};
        if(tm_pmem_open_values(&load,initial,path)) {
            memcpy(tic->ram->persistent.data,m->data.persistent,sizeof initial);
            return -1;
        }
        tm_pmem_close(&load);
    }
    memcpy(tic->ram->persistent.data,initial,sizeof initial);
    memcpy(worker_save_key,key,sizeof worker_save_key);
    worker_capture_pmem=true;
    worker_save_rejected=false;
    return 1;
}
static int prepare_save(tm_studio_session *s) {
    // This thread owns the save context until it publishes save_prepared.
    // Complete the old identity before allowing another RUN request, so
    // returning to that identity cannot read an unfinished background save.
    if(s->save.path && tm_pmem_save_values(&s->save,s->save_values)) return -1;
    char path[PATH_MAX]; u32 initial[256]={0}; tm_pmem next={0};
    if(snprintf(path,sizeof path,"%s/%s.pmem",s->save_folder,s->save_target)>=(int)sizeof path ||
        tm_pmem_open_values(&next,initial,path)) return -1;
    // Keep the old identity and acknowledged values if the new file cannot
    // be opened. Recovery must never depend on reopening the rejected file.
    tm_pmem_close(&s->save); s->save=next;
    memcpy(s->save_key,s->save_target,sizeof s->save_key); s->save_ticks=0;
    // A cancelled or invalid ACK must not flush old-identity values into the
    // prepared new context. Its initial disk values remain the baseline until
    // the parent accepts and queues the new publication.
    memcpy(s->save_values,initial,sizeof s->save_values);
    return 0;
}
static void *save_prepare_loop(void *argument) {
    tm_studio_session *s=argument;
    if(setpriority(PRIO_PROCESS,0,19)) perror("Studio save preparation priority");
    for(;;) {
        pthread_mutex_lock(&s->save_lock);
        while(!s->save_job && !s->stop_save_preparer)
            pthread_cond_wait(&s->save_condition,&s->save_lock);
        bool stop=s->stop_save_preparer; s->save_job=false;
        pthread_mutex_unlock(&s->save_lock);
        if(stop) return NULL;
        s->save_prepare_result=prepare_save(s);
        atomic_store_explicit(&s->save_prepared,true,memory_order_release);
    }
}
static int begin_save_transition(tm_studio_session *s,const char key[33]) {
    if(!s->save_preparer_created) {
        if(pthread_mutex_init(&s->save_lock,NULL)) return -1;
        if(pthread_cond_init(&s->save_condition,NULL)) { pthread_mutex_destroy(&s->save_lock); return -1; }
        if(pthread_create(&s->save_preparer,NULL,save_prepare_loop,s)) {
            pthread_cond_destroy(&s->save_condition); pthread_mutex_destroy(&s->save_lock); return -1;
        }
        s->save_preparer_created=true;
    }
    s->save_transition=true;
    atomic_store_explicit(&s->save_prepared,false,memory_order_release);
    pthread_mutex_lock(&s->save_lock); memcpy(s->save_target,key,sizeof s->save_target); s->save_job=true;
    pthread_cond_signal(&s->save_condition); pthread_mutex_unlock(&s->save_lock);
    return 0;
}
static int save_acknowledged(tm_studio_session *s,const state *data,bool new_identity) {
    if(!*s->save_folder || !*data->pmem_key) return 0;
    if(strcmp(s->save_key,data->pmem_key)) return -1;
    memcpy(s->save_values,data->persistent,sizeof s->save_values);
    if(new_identity || ++s->save_ticks>=60 || (s->data.mode==TIC_RUN_MODE && data->mode!=TIC_RUN_MODE)) {
        s->save_ticks=0;
        bool error=tm_pmem_schedule_values(&s->save,s->save_values)!=0;
        if(error && !s->save_write_error) fprintf(stderr,"Studio persistent memory: background write failed; retry queued\n");
        if(error) s->save_write_error=true;
        s->save_retrying=s->save.worker!=NULL;
    }
    int status=tm_pmem_status(&s->save);
    if(status<0) s->save_write_error=true;
    else if(!status && s->save_retrying) { s->save_write_error=false; s->save_retrying=false; }
    return 0;
}
extern char **environ;
static uint64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static size_t page_size(unsigned page) {
    size_t left=sizeof(tic_cartridge)-page*PAGE_BYTES;
    return left<PAGE_BYTES?left:PAGE_BYTES;
}
#ifdef TM_STUDIO_PROFILE
typedef struct { uint64_t tick, sound, publish; unsigned count; } profile_sample;
static uint64_t profile_scan, profile_hash, profile_output;
static unsigned profile_publications, profile_hashes, profile_dirty_pages;
static uint64_t profile_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
#endif
static int editor(int mode) {
    return mode>=TIC_CONSOLE_MODE && mode<TIC_MODES_COUNT && mode!=TIC_RUN_MODE && mode!=TIC_MENU_MODE;
}
static int background_priority(void) {
    // Hardware playback boosts the frontend. The helper and
    // interpreter must not inherit that boost: they can run while the parent
    // waits, but PCM publication must preempt startup and checkpoint work.
    errno=0;
    int priority=getpriority(PRIO_PROCESS,0);
    if(errno) return -1;
    return priority<0?setpriority(PRIO_PROCESS,0,0):0;
}
static int worker_priority(pid_t parent) {
    // Initialization runs at normal priority. Once ready, give interpretation
    // half the producer's boost so ordinary Linux work cannot starve ticks,
    // while PCM publication can still preempt the interpreter.
    errno=0; int priority=getpriority(PRIO_PROCESS,parent);
    if(errno) return -1;
    if(priority>=0 || !setpriority(PRIO_PROCESS,0,priority/2)) return 0;
    // An inherited producer boost need not imply permission to regain it.
    return errno==EPERM || errno==EACCES?0:-1;
}
static void reap(tm_studio_session *s) {
    if(s->pid>0) {
        kill(-s->pid,SIGKILL); // includes curl children even if Studio has died
        while(waitpid(s->pid,NULL,0)<0 && errno==EINTR) {}
        s->pid=0;
    }
    if(s->socket>=0) { close(s->socket); s->socket=-1; }
    if(s->pending_history_fd>=0) { close(s->pending_history_fd); s->pending_history_fd=-1; }
}
static int receive_ack(tm_studio_session *s) {
    char ack; struct iovec data={&ack,1};
    // Linux SCM_MAX_FD is 253. Receive the entire bounded set so every
    // rejected right can be closed explicitly, including under 32-bit QEMU.
    // The extra capacity covers control-header conversion between ABIs.
    union { struct cmsghdr align; char bytes[CMSG_SPACE(256*sizeof(int))]; } control={0};
    struct msghdr message={.msg_iov=&data,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof control.bytes};
    ssize_t count=recvmsg(s->socket,&message,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
    bool valid=count==1 && ack=='R' && !(message.msg_flags&(MSG_TRUNC|MSG_CTRUNC));
    int descriptor=-1; unsigned descriptors=0;
    if(count>=0) for(struct cmsghdr *c=CMSG_FIRSTHDR(&message);c;c=CMSG_NXTHDR(&message,c)) {
        if(c->cmsg_level!=SOL_SOCKET || c->cmsg_type!=SCM_RIGHTS || c->cmsg_len<CMSG_LEN(0)) { valid=false; continue; }
        size_t bytes=c->cmsg_len-CMSG_LEN(0);
        if(bytes%sizeof(int)) valid=false;
        for(size_t i=0;i<bytes/sizeof(int);++i) {
            int fd; memcpy(&fd,(char*)CMSG_DATA(c)+i*sizeof fd,sizeof fd);
            if(!descriptors++) descriptor=fd; else close(fd);
        }
    }
    if(descriptors>1) valid=false;
    if(!valid) { if(descriptor>=0) close(descriptor); return -1; }
    if(s->pending_history_fd>=0) { if(descriptor>=0) close(descriptor); return -1; }
    s->pending_history_fd=descriptor; return 0;
}
static int receive_response(tm_studio_session *s,unsigned ms) {
        bool new_identity=false;
        if(s->save_failed) return TM_STUDIO_ERROR;
        if(s->save_transition) {
            if(!atomic_load_explicit(&s->save_prepared,memory_order_acquire)) {
                if(ms) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
                return TM_STUDIO_PENDING;
            }
            s->save_transition=false;
            bool cancelled=s->cancel_after_save; s->cancel_after_save=false;
            if(s->save_prepare_result) {
                s->save_rejected=s->save_write_error=true;
                s->save_retrying=false;
                fprintf(stderr,"Studio persistent memory: identity transition could not finish\n");
                return -1;
            }
            s->save_write_error=s->save_retrying=false;
            // The helper must release ownership before recovery can prepare
            // the old identity again. Never accept the cancelled candidate.
            if(cancelled) return -1;
            new_identity=true;
        } else {
        struct pollfd p={s->socket,POLLIN,0};
        int ready=poll(&p,1,(int)(ms>INT_MAX?INT_MAX:ms));
        if(ready<0) return errno==EINTR?TM_STUDIO_PENDING:-1;
        if(!ready) return TM_STUDIO_PENDING;
        if(receive_ack(s)) return -1;
        atomic_thread_fence(memory_order_acquire);
        }
        shared *m=s->ipc;
        u32 history_request=0;
        if((m->history_changed!=0 && m->history_changed!=1) ||
           (m->history_changed!=(s->pending_history_fd>=0)) ||
           (m->history_changed && (!tm_studio_history_snapshot_valid(s->pending_history_fd,&history_request) || history_request!=s->sequence)) ||
           (!m->history_changed && s->history_fd<0)) return -1;
        if(m->magic!=MAGIC || m->acknowledged!=s->sequence || m->sample_count!=1600 ||
            !tm_fft_status_valid(&s->capture,m->capture_status) || m->capture_status==TM_FFT_PAUSED ||
            m->data.mode<0 || m->data.mode>=TIC_MODES_COUNT || !editor(m->data.home) ||
            m->clip_size>=CLIP_BYTES || m->clipboard[m->clip_size] ||
            !memchr(m->data.cwd,0,sizeof m->data.cwd) ||
            !memchr(m->data.name.name,0,sizeof m->data.name.name) ||
            !memchr(m->data.name.path,0,sizeof m->data.name.path) ||
            (m->result!=TM_STUDIO_OK && m->result!=TM_STUDIO_EXIT &&
             m->result!=TM_STUDIO_ERROR && m->result!=TM_STUDIO_SAVE_ERROR && m->result!=TM_STUDIO_CART_ERROR &&
             m->result!=TM_STUDIO_CONFIRM && m->result!=TM_STUDIO_CART_SELECTED && m->result!=TM_STUDIO_CANCELLED) ||
            (m->data.selecting && m->data.mode!=TIC_MENU_MODE) ||
            (m->result==TM_STUDIO_CONFIRM && !m->data.selecting) ||
            ((m->result==TM_STUDIO_CART_SELECTED || m->result==TM_STUDIO_CANCELLED) && m->data.selecting) ||
            (m->result==TM_STUDIO_SAVE_ERROR && m->data.mode==TIC_RUN_MODE)) return -1;
        // Validate the whole publication before changing the private checkpoint.
        for(unsigned i=0;i<sizeof m->data.banks.indexes;++i)
            if(m->data.banks.indexes[i]>=TIC_BANKS) return -1;
        for(unsigned i=0;i<TIC_BANKS;++i)
            if(!tm_studio_sprite_view_valid(&m->data.sprites[i])) return -1;
        if(!tm_studio_code_view_valid(&m->data.code,strnlen(m->cart.code.data,TIC_CODE_SIZE))) return -1;
        if(!save_key_valid(m->data.pmem_key)) return -1;
        for(unsigned i=0;i<PAGES;++i) if(m->dirty[i]>1) return -1;
        if(*s->save_folder && *m->data.pmem_key && strcmp(s->save_key,m->data.pmem_key)) {
            if(begin_save_transition(s,m->data.pmem_key)) {
                s->save_failed=s->save_write_error=true; return TM_STUDIO_ERROR;
            }
            return TM_STUDIO_PENDING;
        }
        int save_result=save_acknowledged(s,&m->data,new_identity);
        if(m->result==TM_STUDIO_SAVE_ERROR) s->save_read_error=true;
        else if(m->data.mode==TIC_RUN_MODE) s->save_read_error=false;
        for(unsigned i=0;i<PAGES;++i) if(m->dirty[i])
            memcpy((u8*)&s->cart+i*PAGE_BYTES,(u8*)&m->cart+i*PAGE_BYTES,page_size(i));
        s->data=m->data;
        s->capture_status=m->capture_status;
        memcpy(s->screen,m->screen,sizeof s->screen); memcpy(s->audio,m->audio,sizeof s->audio);
        s->clip_size=m->clip_size; memcpy(s->clipboard,m->clipboard,m->clip_size+1);
        if(s->pending_history_fd>=0) {
            if(s->history_fd>=0) close(s->history_fd);
            s->history_fd=s->pending_history_fd; s->pending_history_fd=-1;
        }
        return save_result?TM_STUDIO_ERROR:m->result;
}
static int response(tm_studio_session *s,unsigned ms) {
    uint64_t deadline=now_ms()+ms;
    for(;;) {
        uint64_t now=now_ms();
        // The execution deadline bounds the Studio worker. Once its complete
        // ACK arrives, storage preparation waits independently; asynchronous
        // callers continue feeding/pacing the DAC rather than blocking here.
        if(s->save_transition) deadline=now+ms;
        if(now>=deadline) return -1;
        int result=receive_response(s,(unsigned)(deadline-now));
        if(result!=TM_STUDIO_PENDING) return result;
    }
}
static int send_request(tm_studio_session *s,char command) {
    if(!s || s->pid<=0) return -1;
    atomic_store_explicit(&s->ipc->publication.ready,0,memory_order_release);
    atomic_store_explicit(&s->ipc->temporary.ready,0,memory_order_release);
    s->ipc->sequence=++s->sequence;
    s->ipc->save_warning=tm_studio_session_save_error(s);
    atomic_thread_fence(memory_order_release);
    return send(s->socket,&command,1,MSG_NOSIGNAL|MSG_DONTWAIT)==1?0:-1;
}
static int request(tm_studio_session *s,char command,unsigned ms) {
    if(!s || s->pending || !ms || send_request(s,command)) return -1;
    return response(s,ms);
}
static int start_worker(tm_studio_session *s,int restore) {
    shared *m=s->ipc;
    if(restore!=2) m->recovery_request=s->sequence;
    m->magic=MAGIC; m->restore=restore; m->sequence=++s->sequence;
    m->capture=s->capture;
    m->restore_save_failure=s->save_rejected; s->save_rejected=false;
    m->save_warning=tm_studio_session_save_error(s);
    if(restore==1) {
        m->data=s->data; m->data.mode=s->data.home; m->cart=s->cart;
        m->clip_size=s->clip_size; memcpy(m->clipboard,s->clipboard,s->clip_size+1);
    }
    int pair[2]={-1,-1}, memory=-1, socket=-1, history=-1, result=-1;
    if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)) return -1;
    memory=fcntl(s->memory,F_DUPFD_CLOEXEC,10); socket=fcntl(pair[1],F_DUPFD_CLOEXEC,10);
    if(memory<0 || socket<0) goto done;
    if(restore==1 && s->history_fd>=0) {
        history=fcntl(s->history_fd,F_DUPFD_CLOEXEC,10); if(history<0) goto done;
    }
    char parent[32]; snprintf(parent,sizeof parent,"%ld",(long)getpid());
    char *args[]={"tic80-studio","--studio-worker",parent,NULL};
    atomic_thread_fence(memory_order_release);
    pid_t child=0;
    tm_spawn_fd descriptors[]={{memory,3},{socket,4},{history,5}};
    tm_spawn_request spawn={.file="/proc/self/exe",.argv=args,.envp=environ,
        .fds=descriptors,.count=history>=0?3:2,.close_from=history>=0?6:5,.process_group=true};
    int e=tm_spawn_closed(&child,&spawn);
    if(e) goto done;
    s->pid=child;
    s->socket=pair[0]; pair[0]=-1; close(pair[1]); pair[1]=-1;
    result=TM_STUDIO_OK;
done:
    if(memory>=0) close(memory); if(socket>=0) close(socket);
    if(history>=0) close(history);
    for(unsigned i=0;i<2;++i) if(pair[i]>=0) close(pair[i]);
    if(result!=TM_STUDIO_OK) reap(s);
    return result;
}
static int spawn_worker(tm_studio_session *s,int restore) {
    if(start_worker(s,restore)!=TM_STUDIO_OK) return -1;
    int result=response(s,5000); if(result!=TM_STUDIO_OK) reap(s);
    return result;
}
static int recover(tm_studio_session *s) {
    reap(s);
    tic80_input input=s->ipc->input;
    s->suppress_input=input.keyboard.data || input.gamepads.data || input.mouse.left || input.mouse.right || input.mouse.middle;
    return spawn_worker(s,1)==TM_STUDIO_OK?TM_STUDIO_RECOVERED:TM_STUDIO_ERROR;
}
static int cleanup_on_close(tm_studio_session *s,u32 request) {
    if(atomic_load_explicit(&s->ipc->temporary.ready,memory_order_acquire)!=1) return TM_STUDIO_OK;
    s->ipc->recovery_request=request;
    if(start_worker(s,2)!=TM_STUDIO_OK) return TM_STUDIO_ERROR;
    uint64_t deadline=now_ms()+1000; int result=TM_STUDIO_ERROR;
    for(;;) {
        uint64_t now=now_ms(); if(now>=deadline) break;
        struct pollfd socket={s->socket,POLLIN,0};
        int ready=poll(&socket,1,(int)(deadline-now));
        if(ready<0 && errno==EINTR) continue;
        char ack;
        if(ready>0 && recv(s->socket,&ack,1,MSG_DONTWAIT)==1 && ack=='C') result=TM_STUDIO_OK;
        break;
    }
    reap(s); return result;
}
static void *startup_loop(void *argument) {
    tm_studio_session *s=argument;
    int priority=background_priority();
    for(;;) {
        pthread_mutex_lock(&s->startup_lock);
        while(!s->start_job && !s->stop_starter)
            pthread_cond_wait(&s->startup_condition,&s->startup_lock);
        bool stop=s->stop_starter; s->start_job=false;
        pthread_mutex_unlock(&s->startup_lock);
        if(stop) return NULL;
        reap(s); s->startup_result=priority?TM_STUDIO_ERROR:start_worker(s,1);
        atomic_store_explicit(&s->startup_done,true,memory_order_release);
        // Stay alive until the worker is closed: Linux PDEATHSIG tracks the
        // creating thread, including when the rest of its process is alive.
    }
}
int tm_studio_session_open_configured(tm_studio_session **out,const char *folder,const char *save_directory,const tm_fft_config *capture) {
    return tm_studio_session_open_on_cpu(out,folder,save_directory,capture,-1);
}
int tm_studio_session_open_on_cpu(tm_studio_session **out,const char *folder,const char *save_directory,const tm_fft_config *capture,int worker_cpu) {
    if(!out) return TM_STUDIO_ERROR; *out=NULL;
    if(worker_cpu < -1 || worker_cpu>=CPU_SETSIZE) return TM_STUDIO_ERROR;
    const tm_fft_config disabled={0}; if(!capture) capture=&disabled;
    if(!tm_fft_config_valid(capture)) return TM_STUDIO_ERROR;
    // Pinned tic_fs adds a trailing separator inside TICNAME_MAX, even though
    // our transport can hold PATH_MAX. Reject rather than overflow that root.
    if(!folder || !*folder || strlen(folder)>=TICNAME_MAX-1) return TM_STUDIO_ERROR;
    tm_studio_session *s=calloc(1,sizeof *s); if(!s) return TM_STUDIO_ERROR;
    s->capture=*capture;
    atomic_init(&s->pid,0); atomic_init(&s->startup_done,true); atomic_init(&s->save_prepared,true);
    s->socket=s->memory=s->history_fd=s->pending_history_fd=-1; s->ipc=MAP_FAILED;
    if(save_directory) {
        struct stat info;
        if(!realpath(save_directory,s->save_folder) || strlen(s->save_folder)>PATH_MAX-40 ||
            stat(s->save_folder,&info) || !S_ISDIR(info.st_mode)) goto fail;
    }
    s->memory=memfd_create("tic80-studio",MFD_CLOEXEC);
    if(s->memory<0 || ftruncate(s->memory,sizeof(shared))) goto fail;
    s->ipc=mmap(NULL,sizeof(shared),PROT_READ|PROT_WRITE,MAP_SHARED,s->memory,0);
    if(s->ipc==MAP_FAILED) goto fail;
    atomic_init(&s->ipc->publication.ready,0);
    atomic_init(&s->ipc->temporary.ready,0);
    s->ipc->worker_cpu=worker_cpu;
    strcpy(s->ipc->folder,folder);
    strcpy(s->ipc->save_folder,s->save_folder);
    if(spawn_worker(s,0)!=TM_STUDIO_OK) goto fail;
    *out=s; return TM_STUDIO_OK;
fail:
    tm_studio_session_close(s); return TM_STUDIO_ERROR;
}
int tm_studio_session_open_saved(tm_studio_session **out,const char *folder,const char *save_directory)
{ return tm_studio_session_open_configured(out,folder,save_directory,NULL); }
int tm_studio_session_open(tm_studio_session **out,const char *folder)
{ return tm_studio_session_open_saved(out,folder,NULL); }
static int begin_recovery(tm_studio_session *s) {
    tic80_input input=s->ipc->input;
    s->suppress_input=input.keyboard.data || input.gamepads.data || input.mouse.left || input.mouse.right || input.mouse.middle;
    if(!s->starter_created) {
        if(pthread_mutex_init(&s->startup_lock,NULL)) return TM_STUDIO_ERROR;
        if(pthread_cond_init(&s->startup_condition,NULL)) {
            pthread_mutex_destroy(&s->startup_lock); return TM_STUDIO_ERROR;
        }
        if(pthread_create(&s->starter,NULL,startup_loop,s)) {
            pthread_cond_destroy(&s->startup_condition); pthread_mutex_destroy(&s->startup_lock);
            return TM_STUDIO_ERROR;
        }
        s->starter_created=true;
    }
    s->pending=3; s->deadline=now_ms()+5000;
    atomic_store_explicit(&s->startup_done,false,memory_order_release);
    pthread_mutex_lock(&s->startup_lock); s->start_job=true;
    pthread_cond_signal(&s->startup_condition); pthread_mutex_unlock(&s->startup_lock);
    return TM_STUDIO_PENDING;
}
int tm_studio_session_begin_tick(tm_studio_session *s,tic80_input input,uint64_t ns,unsigned ms) {
    if(!s || s->save_failed || s->pending || !ms) return TM_STUDIO_ERROR;
    if(s->suppress_input) {
        if(!input.keyboard.data && !input.gamepads.data && !input.mouse.left && !input.mouse.right && !input.mouse.middle)
            s->suppress_input=false;
        else input=(tic80_input){0};
    }
    s->ipc->input=input; s->ipc->clock_ns=ns;
    if(send_request(s,'T')) return begin_recovery(s)==TM_STUDIO_PENDING?TM_STUDIO_OK:TM_STUDIO_ERROR;
    s->pending=1; s->deadline=now_ms()+ms;
    return TM_STUDIO_OK;
}
static int begin_load(tm_studio_session *s,const u8 *data,size_t size,const char *name,const char *source,unsigned ms,char command) {
    if(!s || s->save_failed || s->pending || s->data.selecting || !ms || !data || size<4 || size>CART_BYTES || !name || strlen(name)>=TICNAME_MAX)
        return TM_STUDIO_ERROR;
    if(source && strlen(source)>=TICNAME_MAX) return TM_STUDIO_ERROR;
    memcpy(s->ipc->load,data,size); s->ipc->cart_size=size; strcpy(s->ipc->load_name,name);
    strcpy(s->ipc->load_source,source?source:"");
    if(send_request(s,command)) return begin_recovery(s)==TM_STUDIO_PENDING?TM_STUDIO_OK:TM_STUDIO_ERROR;
    s->pending=1; s->deadline=now_ms()+ms; return TM_STUDIO_OK;
}
int tm_studio_session_begin_load(tm_studio_session *s,const u8 *data,size_t size,const char *name,unsigned ms)
{ return begin_load(s,data,size,name,NULL,ms,'L'); }
int tm_studio_session_begin_select(tm_studio_session *s,const u8 *data,size_t size,const char *name,unsigned ms)
{ return begin_load(s,data,size,name,NULL,ms,'S'); }
int tm_studio_session_begin_select_source(tm_studio_session *s,const u8 *data,size_t size,const char *name,const char *source,unsigned ms)
{ return begin_load(s,data,size,name,source,ms,'S'); }
int tm_studio_session_begin_run(tm_studio_session *s,unsigned ms) {
    if(!s || s->save_failed || s->pending || s->data.selecting || !ms) return TM_STUDIO_ERROR;
    s->ipc->clock_ns=UINT64_MAX; s->ipc->input=(tic80_input){0};
    if(send_request(s,'R')) return begin_recovery(s)==TM_STUDIO_PENDING?TM_STUDIO_OK:TM_STUDIO_ERROR;
    s->pending=1; s->deadline=now_ms()+ms; return TM_STUDIO_OK;
}
int tm_studio_session_begin_pause(tm_studio_session *s) {
    if(!s || s->save_failed) return TM_STUDIO_ERROR;
    if(s->pending==2 || s->pending==3) return TM_STUDIO_OK;
    if(s->save_transition) { s->cancel_after_save=true; return TM_STUDIO_OK; }
    return begin_recovery(s)==TM_STUDIO_PENDING?TM_STUDIO_OK:TM_STUDIO_ERROR;
}
int tm_studio_session_poll(tm_studio_session *s,unsigned ms) {
    if(!s || !s->pending) return TM_STUDIO_ERROR;
    if(s->pending==3) {
        if(!atomic_load_explicit(&s->startup_done,memory_order_acquire)) {
            if(now_ms()>=s->deadline) return TM_STUDIO_ERROR;
            if(ms) { struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
            return TM_STUDIO_PENDING;
        }
        if(s->startup_result!=TM_STUDIO_OK) { s->pending=0; return TM_STUDIO_ERROR; }
        s->pending=2;
    }
    uint64_t now=now_ms();
    int result=-1;
    if(s->save_transition) {
        result=receive_response(s,ms);
        if(result==TM_STUDIO_PENDING) return result;
    } else if(now<s->deadline) {
        uint64_t remaining=s->deadline-now;
        if(ms>remaining) ms=(unsigned)remaining;
        result=receive_response(s,ms);
        if(result==TM_STUDIO_PENDING && (s->save_transition || now_ms()<s->deadline)) return result;
        if(result==TM_STUDIO_PENDING) result=-1;
    }
    if(result<0) {
        if(s->pending==1) return begin_recovery(s);
        reap(s); s->pending=0; return TM_STUDIO_ERROR;
    }
    int recovering=s->pending==2; s->pending=0;
    return recovering && result==TM_STUDIO_OK?TM_STUDIO_RECOVERED:result;
}
int tm_studio_session_tick(tm_studio_session *s,tic80_input input,uint64_t ns,unsigned ms) {
    if(tm_studio_session_begin_tick(s,input,ns,ms)!=TM_STUDIO_OK) return TM_STUDIO_ERROR;
    int result;
    do result=tm_studio_session_poll(s,ms); while(result==TM_STUDIO_PENDING);
    return result;
}
int tm_studio_session_load(tm_studio_session *s,const u8 *data,size_t size,const char *name) {
    if(!s || s->save_failed || s->pending || s->data.selecting || !data || size>CART_BYTES || size>INT_MAX || !name || strlen(name)>=TICNAME_MAX ||
        !tm_studio_cart_data_valid(data,(s32)size)) return TM_STUDIO_ERROR;
    memcpy(s->ipc->load,data,size); s->ipc->cart_size=size; strcpy(s->ipc->load_name,name);
    s->ipc->load_source[0]=0;
    int result=request(s,'L',5000); return result<0?recover(s):result;
}
int tm_studio_session_load_file(tm_studio_session *s,const char *path,unsigned ms) {
    if(!s || s->save_failed || s->pending || s->data.selecting || !ms || !path || !*path || strlen(path)>=TICNAME_MAX)
        return TM_STUDIO_ERROR;
    strcpy(s->ipc->load_name,path);
    int result=request(s,'F',ms); return result<0?recover(s):result;
}
int tm_studio_session_clipboard(tm_studio_session *s,const char *text) {
    if(!s || s->save_failed || s->pending || !text || strlen(text)>=CLIP_BYTES) return TM_STUDIO_ERROR;
    strcpy(s->ipc->clipboard,text); s->ipc->clip_size=strlen(text);
    int result=request(s,'C',1000); return result<0?recover(s):result;
}
int tm_studio_session_run(tm_studio_session *s,unsigned ms) {
    if(!s || s->data.selecting) return TM_STUDIO_ERROR;
    if(!s || s->save_failed || s->pending || !ms) return TM_STUDIO_ERROR;
    s->ipc->clock_ns=UINT64_MAX; s->ipc->input=(tic80_input){0};
    int result=request(s,'R',ms); return result<0?recover(s):result;
}
int tm_studio_session_close(tm_studio_session *s) {
    if(!s) return TM_STUDIO_OK;
    if(s->pending==3) {
        while(!atomic_load_explicit(&s->startup_done,memory_order_acquire)) {
            struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
        }
    }
    int closed=s->pid>0?TM_STUDIO_ERROR:TM_STUDIO_OK;
    if(s->pending) {
        u32 cleanup_request=s->pending==1?s->sequence:s->ipc->recovery_request;
        reap(s); s->pending=0; closed=cleanup_on_close(s,cleanup_request);
    }
    if(s->pid>0 && request(s,'Q',1000)==TM_STUDIO_OK) {
        uint64_t deadline=now_ms()+500;
        while(now_ms()<deadline) {
            int status; pid_t result=waitpid(s->pid,&status,WNOHANG);
            if(result==s->pid) {
                s->pid=0;
                if(WIFEXITED(status) && !WEXITSTATUS(status)) closed=TM_STUDIO_OK;
                break;
            }
            if(result<0 && errno!=EINTR) break;
            struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
        }
    }
    reap(s);
    if(s->starter_created) {
        pthread_mutex_lock(&s->startup_lock); s->stop_starter=true;
        pthread_cond_signal(&s->startup_condition); pthread_mutex_unlock(&s->startup_lock);
        pthread_join(s->starter,NULL);
        pthread_cond_destroy(&s->startup_condition); pthread_mutex_destroy(&s->startup_lock);
    }
    if(s->save_preparer_created) {
        pthread_mutex_lock(&s->save_lock); s->stop_save_preparer=true;
        pthread_cond_signal(&s->save_condition); pthread_mutex_unlock(&s->save_lock);
        pthread_join(s->save_preparer,NULL);
        pthread_cond_destroy(&s->save_condition); pthread_mutex_destroy(&s->save_lock);
    }
    if(s->memory>=0) close(s->memory);
    if(s->history_fd>=0) close(s->history_fd);
    if(s->ipc!=MAP_FAILED) munmap(s->ipc,sizeof(shared));
    if(s->save.path && tm_pmem_save_values(&s->save,s->save_values)) closed=TM_STUDIO_ERROR;
    if(s->save_failed) closed=TM_STUDIO_ERROR;
    tm_pmem_close(&s->save);
    free(s);
    return closed;
}
const tic_cartridge *tm_studio_session_cart(const tm_studio_session *s) { return &s->cart; }
const u32 *tm_studio_session_screen(const tm_studio_session *s) { return s->screen; }
const s16 *tm_studio_session_audio(const tm_studio_session *s) { return s->audio; }
const u32 *tm_studio_session_persistent(const tm_studio_session *s) { return s->data.persistent; }
const CartName *tm_studio_session_name(const tm_studio_session *s) { return &s->data.name; }
const tm_studio_code_view *tm_studio_session_code_view(const tm_studio_session *s) { return &s->data.code; }
int tm_studio_session_mode(const tm_studio_session *s) { return s->data.mode; }
bool tm_studio_session_modified(const tm_studio_session *s) { return s->data.modified; }
bool tm_studio_session_selecting(const tm_studio_session *s) { return s->data.selecting; }
bool tm_studio_session_save_error(const tm_studio_session *s) { return s->save_write_error || s->save_read_error; }
pid_t tm_studio_session_pid(const tm_studio_session *s) { return s->pid; }
int tm_studio_session_fft_status(const tm_studio_session *s) { return s?s->capture_status:TM_FFT_UNAVAILABLE; }
static void parent_gone(int signal) {
    (void)signal; kill(0,SIGKILL); // own process group, including download helpers
}
static int publish(Studio *studio,shared *m,int result,int force) {
    tic_mem *tic=getMemory(studio);
    if(worker_guard && tm_cart_guard_begin(worker_guard)) return -1;
    state *d=&m->data; d->mode=getStudioMode(studio);
    d->selecting=worker_selecting;
    if(editor(d->mode)) d->home=d->mode;
    d->name=*studioCart(studio); tic_fs_dir(studio_fs(studio),d->cwd);
    d->options=studio_config_get(studio)->data.options;
    tm_studio_bank_state(studio,&d->banks);
    tm_studio_sprite_views(studio,d->sprites);
    tm_studio_code_state(studio,&d->code);
    memcpy(d->pmem_key,worker_save_key,sizeof d->pmem_key);
    tm_studio_caps_state(&d->caps_lock,&d->caps_down);
    u8 saved_hash[16]; tm_studio_saved_hash(studio,saved_hash);
    bool changed=force || memcmp(saved_hash,d->saved_hash,sizeof saved_hash);
    memcpy(d->saved_hash,saved_hash,sizeof saved_hash);
    if(!*m->save_folder || worker_capture_pmem)
        memcpy(d->persistent,tic->ram->persistent.data,sizeof d->persistent);
#ifdef TM_STUDIO_PROFILE
    uint64_t before_scan=profile_now();
#endif
    for(unsigned i=0;i<PAGES;++i) {
        u8 *destination=(u8*)&m->cart+i*PAGE_BYTES;
        const u8 *source=(u8*)&tic->cart+i*PAGE_BYTES;
        bool check=force || !worker_guard || tm_cart_guard_maybe_changed(worker_guard,i*PAGE_BYTES,page_size(i));
        m->dirty[i]=force || (check && !tm_memory_equal(destination,source,page_size(i)));
        if(m->dirty[i]) {
            changed=true; memcpy(destination,source,page_size(i));
#ifdef TM_STUDIO_PROFILE
            ++profile_dirty_pages;
#endif
        }
    }
#ifdef TM_STUDIO_PROFILE
    uint64_t after_scan=profile_now();
    profile_scan+=after_scan-before_scan; ++profile_publications;
    profile_hashes+=changed;
#endif
    if(changed) d->modified=studioCartChanged(studio);
#ifdef TM_STUDIO_PROFILE
    uint64_t after_hash=profile_now(); profile_hash+=after_hash-after_scan;
#endif
    char *clip=tic_sys_clipboard_get(); if(!clip) return -1;
    size_t length=strlen(clip); if(length>=CLIP_BYTES) { tic_sys_clipboard_free(clip); return -1; }
    memcpy(m->clipboard,clip,length+1); m->clip_size=length; tic_sys_clipboard_free(clip);
    m->sample_count=tic->product.samples.count; if(m->sample_count!=1600) return -1;
    memcpy(m->screen,tic->product.screen,sizeof m->screen); memcpy(m->audio,tic->product.samples.buffer,sizeof m->audio);
#ifdef TM_STUDIO_PROFILE
    profile_output+=profile_now()-after_hash;
#endif
    uint64_t revision=tm_studio_history_revision(studio);
    int history=-1; m->history_changed=revision!=worker_history_revision;
    if(m->history_changed) { history=tm_studio_history_snapshot(studio,m->sequence); if(history<0) return -1; }
    m->result=result; m->acknowledged=m->sequence;
    atomic_thread_fence(memory_order_release);
    char ack='R'; struct iovec data={&ack,1};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control={0};
    struct msghdr message={.msg_iov=&data,.msg_iovlen=1};
    if(history>=0) {
        message.msg_control=control.bytes; message.msg_controllen=sizeof control.bytes;
        struct cmsghdr *c=CMSG_FIRSTHDR(&message); c->cmsg_level=SOL_SOCKET; c->cmsg_type=SCM_RIGHTS; c->cmsg_len=CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c),&history,sizeof history);
    }
    bool sent=sendmsg(4,&message,MSG_NOSIGNAL)==1;
    if(history>=0) close(history);
    if(sent) worker_history_revision=revision;
    return sent?0:-1;
}
static void cart_digest(const void *bytes,size_t size,u8 out[16]) {
    MD5_CTX hash; MD5_Init(&hash); MD5_Update(&hash,bytes,(unsigned long)size); MD5_Final(out,&hash);
}
static bool remember_temporary(const char *path,int descriptor,void *argument) {
    shared *m=argument; save_temporary *t=&m->temporary;
    atomic_store_explicit(&t->ready,0,memory_order_release);
    char resolved[PATH_MAX]; struct stat descriptor_info,path_info;
    if(!realpath(path,resolved) || strlen(resolved)>=sizeof t->path ||
       fstat(descriptor,&descriptor_info) || !S_ISREG(descriptor_info.st_mode) ||
       lstat(resolved,&path_info) || !S_ISREG(path_info.st_mode) ||
       path_info.st_dev!=descriptor_info.st_dev || path_info.st_ino!=descriptor_info.st_ino) return false;
    const char *base=strrchr(resolved,'/');
    if(!base || strncmp(base+1,".tic80-cart-",12) || strlen(base+1)!=18) return false;
    t->request=m->sequence; strcpy(t->path,resolved);
    t->device=(uint64_t)descriptor_info.st_dev; t->inode=(uint64_t)descriptor_info.st_ino;
    atomic_store_explicit(&t->ready,1,memory_order_release);
    return true;
}
static bool cleanup_temporary(shared *m) {
    save_temporary *t=&m->temporary;
    if(!m->restore || atomic_load_explicit(&t->ready,memory_order_acquire)!=1 ||
       t->request!=m->recovery_request) return true;
    bool result=true;
    if(memchr(t->path,0,sizeof t->path) && *t->path=='/') {
        const char *base=strrchr(t->path,'/');
        if(base && !strncmp(base+1,".tic80-cart-",12) && strlen(base+1)==18) {
            int fd=open(t->path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
            struct stat opened,named;
            bool owned=fd>=0 && !fstat(fd,&opened) && S_ISREG(opened.st_mode) &&
               (uint64_t)opened.st_dev==t->device && (uint64_t)opened.st_ino==t->inode &&
               !lstat(t->path,&named) && S_ISREG(named.st_mode) &&
               named.st_dev==opened.st_dev && named.st_ino==opened.st_ino;
            if(owned && unlink(t->path) && errno!=ENOENT) {
                fprintf(stderr,"Studio Save: interrupted temporary file could not be removed\n");
                result=false;
            }
            if(fd>=0) close(fd);
        }
    }
    atomic_store_explicit(&t->ready,0,memory_order_release);
    return result;
}
static bool prepare_publication(Studio *studio,const char *binding,const char *target,
                                int descriptor,const void *bytes,s32 size,void *argument) {
    shared *m=argument; save_publication *p=&m->publication;
    atomic_store_explicit(&p->ready,0,memory_order_release);
    struct stat info;
    if(size<=0 || strlen(binding)>=TICNAME_MAX || strlen(target)>=sizeof p->target ||
       fstat(descriptor,&info) || !S_ISREG(info.st_mode) || info.st_size!=size) return false;
    const char *base=strrchr(binding,'/'); base=base?base+1:binding;
    if(!*base || strlen(base)>=TICNAME_MAX) return false;
    p->request=m->sequence; p->previous=*studioCart(studio);
    strcpy(p->destination.name,base); strcpy(p->destination.path,binding); strcpy(p->target,target);
    p->device=(uint64_t)info.st_dev; p->inode=(uint64_t)info.st_ino; p->size=(uint64_t)size;
    cart_digest(&getMemory(studio)->cart,sizeof(tic_cartridge),p->cart_hash);
    cart_digest(bytes,(size_t)size,p->file_hash);
    // Complete immutable intent precedes rename; inode/content verification
    // resolves death on either side of publication without a final worker ACK.
    atomic_store_explicit(&p->ready,1,memory_order_release);
    return true;
}
static bool reconcile_publication(shared *m) {
    save_publication *p=&m->publication;
    if(!m->restore || atomic_load_explicit(&p->ready,memory_order_acquire)!=1 ||
       p->request!=m->recovery_request) return false;
    bool valid=memchr(p->previous.name,0,TICNAME_MAX) && memchr(p->previous.path,0,TICNAME_MAX) &&
        memchr(p->destination.name,0,TICNAME_MAX) && memchr(p->destination.path,0,TICNAME_MAX) &&
        memchr(p->target,0,sizeof p->target) && *p->destination.name && *p->destination.path && *p->target &&
        p->size>0 && p->size<=3*sizeof(tic_cartridge) &&
        !strcmp(p->previous.name,m->data.name.name) && !strcmp(p->previous.path,m->data.name.path);
    u8 hash[16];
    if(valid) { cart_digest(&m->cart,sizeof m->cart,hash); valid=!memcmp(hash,p->cart_hash,16); }
    int fd=valid?open(p->target,O_RDONLY|O_NONBLOCK|O_CLOEXEC):-1;
    struct stat before,after,binding;
    valid=valid && fd>=0 && !fstat(fd,&before) && S_ISREG(before.st_mode) &&
        (uint64_t)before.st_dev==p->device && (uint64_t)before.st_ino==p->inode &&
        (uint64_t)before.st_size==p->size;
    MD5_CTX digest; MD5_Init(&digest);
    uint64_t count=0; u8 block[4096];
    while(valid && count<p->size) {
        size_t want=p->size-count>sizeof block?sizeof block:(size_t)(p->size-count);
        ssize_t got=read(fd,block,want);
        if(got<0 && errno==EINTR) continue;
        if(got<=0) { valid=false; break; }
        MD5_Update(&digest,block,(unsigned long)got); count+=(uint64_t)got;
    }
    MD5_Final(hash,&digest);
    if(valid) valid=!memcmp(hash,p->file_hash,16) && !fstat(fd,&after) &&
        before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_size==after.st_size &&
        before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec &&
        !stat(p->destination.path,&binding) && binding.st_dev==before.st_dev && binding.st_ino==before.st_ino;
    if(fd>=0 && close(fd)) valid=false;
    if(valid) m->data.name=p->destination;
    atomic_store_explicit(&p->ready,0,memory_order_release);
    return valid;
}
static void worker_studio_delete(Studio *studio)
{ tm_cart_guard_close(worker_guard); worker_guard=NULL; tm_fft_worker_close(); studio_delete(studio); }
int tm_studio_session_worker(int argc,char **argv) {
    if(argc!=3) return 2;
    char *end; errno=0; long parent=strtol(argv[2],&end,10);
    if(errno || !*argv[2] || *end || parent<=1 || getpgrp()!=getpid()) return 2;
    if(background_priority()) return 1;
    struct sigaction action={0}; action.sa_handler=parent_gone; sigemptyset(&action.sa_mask);
    if(sigaction(SIGTERM,&action,NULL) || prctl(PR_SET_PDEATHSIG,SIGTERM)) return 1;
    if(getppid()!=parent) parent_gone(SIGTERM);
    prctl(PR_SET_NAME,"tic80-studio");
#ifndef __SANITIZE_ADDRESS__
    struct rlimit limit={256u*1024u*1024u,256u*1024u*1024u};
    if(setrlimit(RLIMIT_AS,&limit)) return 1;
#endif
    struct rlimit cores={0,0}; if(setrlimit(RLIMIT_CORE,&cores)) return 1;
    struct stat info; if(fstat(3,&info) || info.st_size!=sizeof(shared)) return 2;
    shared *m=mmap(NULL,sizeof(shared),PROT_READ|PROT_WRITE,MAP_SHARED,3,0);
    close(3); if(m==MAP_FAILED) return 1;
    atomic_thread_fence(memory_order_acquire);
    if(m->magic!=MAGIC || m->restore<0 || m->restore>2 || !tm_fft_config_valid(&m->capture) || !memchr(m->folder,0,sizeof m->folder) ||
        !*m->folder || strlen(m->folder)>=TICNAME_MAX-1 ||
        !memchr(m->save_folder,0,sizeof m->save_folder) ||
        (m->restore_save_failure!=0 && m->restore_save_failure!=1) ||
        (m->save_warning!=0 && m->save_warning!=1)) return 2;
    if(m->worker_cpu < -1 || m->worker_cpu>=CPU_SETSIZE) return 2;
    if(m->worker_cpu>=0) {
        cpu_set_t cpu; CPU_ZERO(&cpu); CPU_SET(m->worker_cpu,&cpu);
        if(sched_setaffinity(0,sizeof cpu,&cpu)) { munmap(m,sizeof *m); return 1; }
    }
    if(m->restore==2) {
        char ack=cleanup_temporary(m)?'C':'E';
        int sent=send(4,&ack,1,MSG_NOSIGNAL)==1;
        munmap(m,sizeof *m); return sent?0:1;
    }
    bool recovered_publication=reconcile_publication(m);
    cleanup_temporary(m);
    char *args[]={"tic80-studio","--skip",NULL};
    Studio *studio=studio_create(2,args,48000,TIC80_PIXEL_COLOR_RGBA8888,m->folder,1,tic_layout_qwerty);
    if(!studio) { munmap(m,sizeof *m); return 1; }
    m->capture_status=tm_fft_worker_start(&m->capture);
    studio_config_get(studio)->data.fft=m->capture.enabled;
    studio_config_get(studio)->data.fftdevice=m->capture.device;
    tm_studio_bind(studio); studio_config_get(studio)->data.checkNewVersion=false;
    if(m->clock_ns==UINT64_MAX) tm_studio_real_clock(); else tm_studio_clock(m->clock_ns);
    setStudioMode(studio,TIC_CONSOLE_MODE);
    // Console's first tick loads its demo. Consume that initialization before
    // restoring edits, including when recovery returns to another editor.
    tm_studio_tick(studio,(tic80_input){0}); studio_sound(studio);
    if(*m->save_folder) {
        if(m->restore) {
            if(!save_key_valid(m->data.pmem_key)) { worker_studio_delete(studio); munmap(m,sizeof *m); return 2; }
            memcpy(worker_save_key,m->data.pmem_key,sizeof worker_save_key);
        }
        tm_studio_pmem_hook(worker_save_load,m);
    }
    if(m->restore) {
        getMemory(studio)->cart=m->cart; studioRomLoaded(studio);
        if(fcntl(5,F_GETFD)<0 || !tm_studio_history_restore(studio,5)) { worker_studio_delete(studio); munmap(m,sizeof *m); return 2; }
        close(5);
        studioSetCartName(studio,m->data.name.name,m->data.name.path);
        tm_studio_restore_saved_hash(studio,m->data.saved_hash);
        studio_config_get(studio)->data.options=m->data.options;
        if(!tm_studio_restore_banks(studio,&m->data.banks)) { worker_studio_delete(studio); munmap(m,sizeof *m); return 2; }
        if(!tm_studio_restore_sprite_views(studio,m->data.sprites)) { worker_studio_delete(studio); munmap(m,sizeof *m); return 2; }
        // setStudioMode resets Vi state; select the home editor before restoring it.
        setStudioMode(studio,m->data.home);
        if(!tm_studio_restore_code(studio,&m->data.code)) { worker_studio_delete(studio); munmap(m,sizeof *m); return 2; }
        tic_fs_homedir(studio_fs(studio)); if(*m->data.cwd) tic_fs_changedir(studio_fs(studio),m->data.cwd);
        memcpy(getMemory(studio)->ram->persistent.data,m->data.persistent,sizeof m->data.persistent);
        tic_sys_clipboard_set(m->clipboard);
        tm_studio_restore_caps(m->data.caps_lock,m->data.caps_down);
        tm_studio_popup(studio,recovered_publication?"Save published; edits kept. Retry Save.":m->restore_save_failure?
            "Save failed; edits kept. Retry RUN.":"Run stopped; completed edits kept");
    } else m->data.home=TIC_CONSOLE_MODE;
    tm_studio_tick(studio,(tic80_input){0}); studio_sound(studio);
    tm_studio_cart_publish_hook(prepare_publication,m);
    tm_studio_cart_temp_hook(remember_temporary,m);
    if(worker_priority((pid_t)parent)) { worker_studio_delete(studio); munmap(m,sizeof *m); return 1; }
    worker_guard=tm_cart_guard_open(&getMemory(studio)->cart,sizeof(tic_cartridge));
    int status=publish(studio,m,TM_STUDIO_OK,1);
    bool warned=m->save_warning;
    unsigned warning_ticks=0;
#ifdef TM_STUDIO_PROFILE
    profile_sample profile[TIC_MODES_COUNT]={0};
#endif
    while(!status) {
        char command; if(recv(4,&command,1,0)!=1) break;
        atomic_thread_fence(memory_order_acquire);
        if(command=='Q') break;
        if(m->save_warning!=0 && m->save_warning!=1) { status=-1; break; }
        if(command=='T' || command=='R') {
            // Let the popup finish unrolling instead of restarting its
            // animation every frame. Refresh before its two-second expiry.
            if(m->save_warning && (!warned || ++warning_ticks>=100)) {
                tm_studio_popup(studio,"Save failed; edits kept. Retry RUN."); warning_ticks=0;
            } else if(!m->save_warning && warned) {
                tm_studio_popup(studio,"Save storage recovered"); warning_ticks=0;
            }
            warned=m->save_warning;
        }
        int result=TM_STUDIO_OK;
        worker_selection_result=TM_STUDIO_OK;
        worker_save_rejected=false;
        worker_capture_pmem=getStudioMode(studio)==TIC_RUN_MODE && (command=='T' || command=='R');
#ifdef TM_STUDIO_PROFILE
        uint64_t start=profile_now(), after_tick=0, after_sound=0;
#endif
        if(command=='T') {
            if(m->clock_ns==UINT64_MAX) tm_studio_real_clock(); else tm_studio_clock(m->clock_ns);
            tm_studio_tick(studio,m->input);
            // Upstream shortcuts can leave a dialog (ESC or an editor key)
            // without invoking its YES/NO callback. Treat that as cancellation.
            if(worker_selecting && getStudioMode(studio)!=TIC_MENU_MODE) {
                tm_studio_cancel_confirmation(studio);
            }
#ifdef TM_STUDIO_PROFILE
            after_tick=profile_now();
#endif
            studio_sound(studio);
#ifdef TM_STUDIO_PROFILE
            after_sound=profile_now();
#endif
            if(studio_alive(studio)) result=TM_STUDIO_EXIT;
        } else if(command=='S') {
            if(worker_selecting) result=TM_STUDIO_ERROR;
            else if(m->cart_size>CART_BYTES || !memchr(m->load_name,0,sizeof m->load_name) ||
                !memchr(m->load_source,0,sizeof m->load_source) ||
                !tm_studio_cart_data_valid(m->load,m->cart_size)) {
                tm_studio_popup(studio,"Cart download failed"); result=TM_STUDIO_CART_ERROR;
            } else {
                if(studioCartChanged(studio)) {
                    worker_selecting=true; confirmLoadCart(studio,selection_confirmed,m); result=TM_STUDIO_CONFIRM;
                } else { selection_confirmed(studio,true,m); result=worker_selection_result; }
            }
        } else if(command=='L') {
            bool was_run=getStudioMode(studio)==TIC_RUN_MODE;
            if(m->cart_size>CART_BYTES || !memchr(m->load_name,0,sizeof m->load_name) ||
                !tm_studio_hashload_apply(studio,m->load,m->cart_size,m->load_name,NULL)) result=TM_STUDIO_CART_ERROR;
            else if(was_run) setStudioMode(studio,m->data.home);
        } else if(command=='F') {
            bool was_run=getStudioMode(studio)==TIC_RUN_MODE;
            if(!memchr(m->load_name,0,sizeof m->load_name) ||
                !tm_studio_cart_file_apply(studio,m->load_name)) result=TM_STUDIO_CART_ERROR;
            else if(was_run) setStudioMode(studio,m->data.home);
        } else if(command=='R') {
            tm_studio_real_clock(); runGame(studio,RUN_FROM_STUDIO);
            if(getStudioMode(studio)!=TIC_RUN_MODE) result=TM_STUDIO_ERROR;
            else { tm_studio_tick(studio,(tic80_input){0}); studio_sound(studio); }
            if(studio_alive(studio)) result=TM_STUDIO_EXIT;
        } else if(command=='C') {
            if(m->clip_size>=CLIP_BYTES || m->clipboard[m->clip_size]) { status=-1; break; }
            tic_sys_clipboard_set(m->clipboard);
        } else { status=-1; break; }
        if(command=='T' && worker_selection_result!=TM_STUDIO_OK) result=worker_selection_result;
        if(worker_save_rejected) result=TM_STUDIO_SAVE_ERROR;
        status=publish(studio,m,result,0);
#ifdef TM_STUDIO_PROFILE
        if(command=='T') {
            int mode=getStudioMode(studio);
            if(mode>=0 && mode<TIC_MODES_COUNT) {
                profile_sample *p=&profile[mode]; ++p->count;
                p->tick+=after_tick-start; p->sound+=after_sound-after_tick;
                p->publish+=profile_now()-after_sound;
            }
        }
#endif
    }
#ifdef TM_STUDIO_PROFILE
    double publication_scale=profile_publications*1000000.0;
    fprintf(stderr,"Studio checkpoint profile: publications=%u dirty_pages=%u hashes=%u scan_ms=%.3f hash_ms=%.3f output_ms=%.3f\n",
        profile_publications,profile_dirty_pages,profile_hashes,
        profile_scan/publication_scale,profile_hash/publication_scale,profile_output/publication_scale);
    for(unsigned mode=0;mode<TIC_MODES_COUNT;++mode) if(profile[mode].count) {
        profile_sample *p=&profile[mode]; double scale=p->count*1000000.0;
        fprintf(stderr,"Studio profile: mode=%u ticks=%u tick_ms=%.3f sound_ms=%.3f publish_ms=%.3f\n",
            mode,p->count,p->tick/scale,p->sound/scale,p->publish/scale);
    }
#endif
    worker_studio_delete(studio); tm_studio_bind(NULL);
    // Normal close acknowledges only after download helpers have been reaped.
    if(!status) { m->history_changed=0; m->result=TM_STUDIO_OK; m->acknowledged=m->sequence; atomic_thread_fence(memory_order_release); send(4,"R",1,MSG_NOSIGNAL); }
    munmap(m,sizeof *m); close(4); return status?1:0;
}
