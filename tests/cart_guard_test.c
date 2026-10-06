#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/cart_guard_private.h"
#include <assert.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static volatile sig_atomic_t forwarded;
static void prior(int signal,siginfo_t *info,void *context) {
    (void)signal;(void)info;(void)context;forwarded=1;_exit(37);
}
static void invalid_access(void) {
    volatile unsigned char *bad=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(bad!=MAP_FAILED);*bad=1;
}
int main(void) {
#if defined(__SANITIZE_ADDRESS__)
    char sanitized[8192]={0};
    assert(!tm_cart_guard_open(sanitized,sizeof sanitized));
    assert(tm_cart_guard_maybe_changed(NULL,0,4096));
    assert(tm_cart_guard_begin(NULL)==-1);tm_cart_guard_close(NULL);
    puts("Guard: sanitizer fallback retains full comparisons");return 0;
#endif
    size_t page=sysconf(_SC_PAGESIZE),length=page*19+101;
    unsigned char *allocation=mmap(NULL,length+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(allocation!=MAP_FAILED);
    unsigned char *source=allocation+37,*copy=calloc(length,1);assert(copy);
    tm_cart_guard *g=tm_cart_guard_open(source,length);assert(g);assert(!tm_cart_guard_open(source,length));
    assert(!tm_cart_guard_begin(g));memcpy(copy,source,length);assert(!tm_cart_guard_begin(g));
    for(size_t round=0;round<300;++round) {
        size_t offsets[]={0,length-1,round*997%length,round*page%length,(round*page+page-1)%length};
        for(size_t i=0;i<sizeof offsets/sizeof offsets[0];++i) source[offsets[i]]^=(unsigned char)(round+i+1);
        assert(!tm_cart_guard_begin(g));
        for(size_t offset=0;offset<length;offset+=page) {
            size_t size=length-offset<page?length-offset:page;
            bool actual=memcmp(source+offset,copy+offset,size)!=0;
            assert(!actual || tm_cart_guard_maybe_changed(g,offset,size));
            if(tm_cart_guard_maybe_changed(g,offset,size)) memcpy(copy+offset,source+offset,size);
        }
        assert(!memcmp(copy,source,length));
        // A mutation during capture must survive the next rearm.
        source[page*7+83]^=1;
        assert(tm_cart_guard_maybe_changed(g,page*7,page));
        assert(!tm_cart_guard_begin(g));assert(tm_cart_guard_maybe_changed(g,page*7,page));memcpy(copy,source,length);
        assert(!tm_cart_guard_begin(g));assert(!tm_cart_guard_maybe_changed(g,page*7,page));
    }
    pid_t child=fork();assert(child>=0);if(!child) {invalid_access();_exit(0);}int status;assert(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGSEGV);
    child=fork();assert(child>=0);if(!child) {alarm(2);((void (*)(void))(allocation+page*2))();_exit(0);}assert(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGSEGV);
    tm_cart_guard_close(g);memset(source,3,length);assert(!munmap(allocation,length+page));free(copy);
    struct sigaction action={0};action.sa_sigaction=prior;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);assert(!sigaction(SIGSEGV,&action,NULL));
    allocation=mmap(NULL,page*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(allocation!=MAP_FAILED);g=tm_cart_guard_open(allocation,page*4);assert(g);
    child=fork();assert(child>=0);if(!child) {invalid_access();_exit(0);}assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==37);
    tm_cart_guard_close(g);assert(!sigaction(SIGSEGV,NULL,&action) && action.sa_sigaction==prior);assert(!munmap(allocation,page*4));
    action.sa_handler=SIG_IGN;action.sa_flags=0;assert(!sigaction(SIGSEGV,&action,NULL));
    allocation=mmap(NULL,page*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(allocation!=MAP_FAILED);g=tm_cart_guard_open(allocation,page*4);assert(g);assert(!raise(SIGSEGV));tm_cart_guard_close(g);assert(!sigaction(SIGSEGV,NULL,&action) && action.sa_handler==SIG_IGN);assert(!munmap(allocation,page*4));
    puts("Guard: unaligned boundaries, 300 mutation rounds, capture-time writes, exact checkpoint bytes, unrelated SIGSEGV, NX execution fault and prior handler restoration passed");
}
