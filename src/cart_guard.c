#define _GNU_SOURCE
#include "tic80_mister/cart_guard_private.h"
#include <stdint.h>
#include <stdatomic.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sched.h>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TM_GUARD_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(TM_GUARD_ASAN)
tm_cart_guard *tm_cart_guard_open(void *bytes,size_t size) {(void)bytes;(void)size;return NULL;}
int tm_cart_guard_begin(tm_cart_guard *g) {(void)g;return -1;}
bool tm_cart_guard_maybe_changed(const tm_cart_guard *g,size_t offset,size_t size) {(void)g;(void)offset;(void)size;return true;}
void tm_cart_guard_close(tm_cart_guard *g) {(void)g;}
#else
_Static_assert(ATOMIC_CHAR_LOCK_FREE==2,"Signal dirty flags must be lock-free");
_Static_assert(ATOMIC_POINTER_LOCK_FREE==2 && ATOMIC_INT_LOCK_FREE==2,"Signal ownership must be lock-free");
struct tm_cart_guard {
    uintptr_t bytes,first,end;
    size_t size,page,count;
    _Atomic unsigned char *dirty,*writable;
    unsigned char *previous;
    struct sigaction original;
};
static _Atomic(tm_cart_guard *) active;
static atomic_uint handlers;
static struct sigaction previous_action;
static void forward(int signal,siginfo_t *info,void *context,struct sigaction original) {
    if(original.sa_handler==SIG_IGN) return;
    if(original.sa_handler==SIG_DFL) {
        // An unrelated invalid access retains ordinary SIGSEGV termination.
        struct sigaction action={0};action.sa_handler=SIG_DFL;sigemptyset(&action.sa_mask);
        sigaction(signal,&action,NULL);raise(signal);return;
    }
    if(original.sa_flags&SA_SIGINFO) original.sa_sigaction(signal,info,context);
    else original.sa_handler(signal);
}
static void fault(int signal,siginfo_t *info,void *context) {
    int saved=errno;atomic_fetch_add(&handlers,1);tm_cart_guard *g=atomic_load(&active);uintptr_t address=(uintptr_t)info->si_addr;
    if(g && info->si_code==SEGV_ACCERR && address>=g->first && address<g->end) {
        size_t index=(address-g->first)/g->page;
        if(!atomic_exchange_explicit(&g->writable[index],1,memory_order_relaxed)) {
        atomic_store_explicit(&g->dirty[index],1,memory_order_relaxed);
        // Linux mprotect is a direct syscall here: no allocator/stdio/locks in
        // the fault path. The source cartridge originally resides on RW heap.
        if(!syscall(SYS_mprotect,(void*)(g->first+index*g->page),g->page,PROT_READ|PROT_WRITE)) {atomic_fetch_sub(&handlers,1);errno=saved;return;}
        }
        // A second permission fault on a page already made writable is not a
        // tracked write (for example, execution of non-executable cart data).
        // Preserve the prior fault behavior instead of retrying indefinitely.
    }
    struct sigaction original=previous_action;
    atomic_fetch_sub(&handlers,1);
    forward(signal,info,context,original);errno=saved;
}
tm_cart_guard *tm_cart_guard_open(void *bytes,size_t size) {
    if(atomic_load(&active) || !bytes || !size) return NULL;
    long page=sysconf(_SC_PAGESIZE);uintptr_t start=(uintptr_t)bytes;
    if(page<=0 || (page&(page-1)) || size>UINTPTR_MAX-start || start>UINTPTR_MAX-(size_t)page) return NULL;
    uintptr_t first=(start+(size_t)page-1)&~((uintptr_t)page-1),end=(start+size)&~((uintptr_t)page-1);
    if(end<=first) return NULL;
    tm_cart_guard *g=calloc(1,sizeof *g);if(!g) return NULL;
    g->bytes=start;g->size=size;g->first=first;g->end=end;g->page=page;g->count=(end-first)/page;
    g->dirty=calloc(g->count,sizeof *g->dirty);g->writable=calloc(g->count,sizeof *g->writable);g->previous=calloc(g->count,1);
    if(!g->dirty || !g->writable || !g->previous) goto fail;
    for(size_t i=0;i<g->count;++i) {atomic_init(&g->dirty[i],1);atomic_init(&g->writable[i],0);}
    struct sigaction action={0};action.sa_sigaction=fault;action.sa_flags=SA_SIGINFO|SA_RESTART;sigemptyset(&action.sa_mask);
    if(sigaction(SIGSEGV,NULL,&g->original)) goto fail;
    previous_action=g->original;atomic_store(&active,g);
    if(sigaction(SIGSEGV,&action,NULL)) {atomic_store(&active,NULL);goto fail;}
    if(mprotect((void*)first,end-first,PROT_READ)) {tm_cart_guard_close(g);return NULL;}
    return g;
fail:free(g->dirty);free(g->writable);free(g->previous);free(g);return NULL;
}
int tm_cart_guard_begin(tm_cart_guard *g) {
    if(!g || g!=atomic_load(&active)) return -1;
    bool changed=false;
    // Called between Studio requests on the sole cartridge-writing thread.
    // Rearm before capture, so writes by getters/hash helpers remain dirty for
    // the next publication instead of being discarded at the end of capture.
    for(size_t i=0;i<g->count;++i) {
        g->previous[i]=atomic_exchange_explicit(&g->dirty[i],0,memory_order_relaxed);
        changed|=g->previous[i]!=0;
    }
    if(changed) {
        for(size_t i=0;i<g->count;++i) atomic_store_explicit(&g->writable[i],0,memory_order_relaxed);
        if(mprotect((void*)g->first,g->end-g->first,PROT_READ)) return -1;
    }
    return 0;
}
bool tm_cart_guard_maybe_changed(const tm_cart_guard *g,size_t offset,size_t size) {
    if(!g || !size || offset>g->size || size>g->size-offset) return true;
    uintptr_t start=g->bytes+offset,end=start+size;
    if(start<g->first || end>g->end) return true;
    for(size_t i=(start-g->first)/g->page;i<=(end-1-g->first)/g->page;++i)
        if(g->previous[i] || atomic_load_explicit(&g->dirty[i],memory_order_relaxed)) return true;
    return false;
}
void tm_cart_guard_close(tm_cart_guard *g) {
    if(!g) return;
    if(g==atomic_load(&active)) {
        // Restore before freeing Studio/cart allocations. A failed restore is
        // fatal to this worker, never a silently inaccessible heap mapping.
        if(mprotect((void*)g->first,g->end-g->first,PROT_READ|PROT_WRITE)) _exit(1);
        if(sigaction(SIGSEGV,&g->original,NULL)) _exit(1);
        atomic_store(&active,NULL);
        while(atomic_load(&handlers)) sched_yield();
    }
    free(g->dirty);free(g->writable);free(g->previous);free(g);
}
#endif
