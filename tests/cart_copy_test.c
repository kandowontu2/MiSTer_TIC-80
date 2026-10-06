#define _GNU_SOURCE
#include "tic80_mister/backend.h"
#include "tic80_mister/memory_map.h"
#include "tic80_mister/studio_session.h"
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
#define REG(off) (*(volatile uint32_t *)(memory+(off)))
static uint8_t *memory;
static int core_fd=-1, fault, deferred_socket=-1, deferred_flags;
static uint32_t previous_ack;
static int defer_selection;
static char deferred_command;
static void core_name(const char *name)
{ CHECK(lseek(core_fd,0,SEEK_SET)==0 && !ftruncate(core_fd,0)); CHECK(write(core_fd,name,strlen(name))==(ssize_t)strlen(name)); }
void *__real_memcpy(void*,const void*,size_t);
void *__real___memcpy_chk(void*,const void*,size_t,size_t);
static void before_copy(const void *from)
{
    if(memory && (from==memory+TM_CART_DATA_OFFSET || from==memory+TM_CART_SOURCE_OFFSET+8))
        CHECK(REG(TM_CART_ACK_OFFSET)==previous_ack);
}
static void after_copy(const void *from)
{
    if(memory && from==memory+TM_CART_DATA_OFFSET && fault) {
        int action=fault; fault=0;
        if(action==1) REG(TM_SESSION_ACK_OFFSET)++;
        else if(action==2) REG(TM_CART_META_OFFSET)+=4;
        else if(action==3) core_name("MENU");
    }
}
void *__wrap_memcpy(void *to,const void *from,size_t size)
{ before_copy(from); void *result=__real_memcpy(to,from,size); after_copy(from); return result; }
void *__wrap___memcpy_chk(void *to,const void *from,size_t size,size_t capacity)
{ before_copy(from); void *result=__real___memcpy_chk(to,from,size,capacity); after_copy(from); return result; }
size_t __real_fread(void*,size_t,size_t,FILE*);
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *file)
{
    size_t result=__real_fread(out,size,count,file);
    if(fault==4 && size*result==6 && !memcmp(out,"TIC-80",6)) { fault=0; core_name("MENU"); }
    return result;
}
ssize_t __real_send(int,const void*,size_t,int);
ssize_t __wrap_send(int socket,const void *bytes,size_t size,int flags)
{
    if(defer_selection && size==1 && *(const char*)bytes=='S') {
        CHECK(deferred_socket==-1); deferred_socket=socket; deferred_flags=flags;
        deferred_command='S'; defer_selection=0; return 1;
    }
    return __real_send(socket,bytes,size,flags);
}
static uint64_t now_ns(void)
{ struct timespec t; CHECK(!clock_gettime(CLOCK_MONOTONIC,&t)); return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec; }
static int remove_entry(const char *path,const struct stat *info,int type,struct FTW *walk)
{ (void)info; (void)type; (void)walk; return remove(path); }
static void metric(const char *phase,size_t bytes,const uint64_t *times,unsigned count)
{
    uint64_t minimum=times[0],maximum=times[0],sum=0;
    for(unsigned i=0;i<count;++i) { if(times[i]<minimum) minimum=times[i]; if(times[i]>maximum) maximum=times[i]; sum+=times[i]; }
    printf("COPY_METRIC phase=%s bytes=%zu samples=%u min_ms=%.6f mean_ms=%.6f max_ms=%.6f\n",
           phase,bytes,count,minimum/1e6,(double)sum/count/1e6,maximum/1e6);
}
static void prepare(tm_backend *backend,const char *core_path,size_t bytes,uint32_t ticket,const char *source)
{
    core_name("TIC-80");
    REG(TM_IDENTITY_OFFSET)=TM_MAGIC; REG(TM_IDENTITY_OFFSET+4)=TM_CART_SOURCE_MAGIC;
    REG(TM_GEOMETRY_OFFSET)=TM_WIDTH|(TM_HEIGHT<<16);
    REG(TM_SESSION_REQUEST_OFFSET)=17; REG(TM_SESSION_ACK_OFFSET)=17;
    *backend=(tm_backend){.memory=memory,.fd=-1,.session=17,.core_name_path=core_path};
    REG(TM_CART_META_OFFSET)=ticket; REG(TM_CART_META_OFFSET+4)=bytes;
    REG(TM_CART_SOURCE_OFFSET)=ticket; REG(TM_CART_SOURCE_OFFSET+4)=strlen(source)+1;
    memcpy(memory+TM_CART_SOURCE_OFFSET+8,source,strlen(source)+1);
    REG(TM_CART_ACK_OFFSET)=previous_ack=0;
}
static void lost(const char *kind,const char *core_path,const char *source)
{
    tm_backend backend;
    prepare(&backend,core_path,TM_CART_CAPACITY,6,source);
    uint8_t *copy=(void*)1; size_t size=99; char path[256]="previous source";
    if(!strcmp(kind,"lost-entry")) core_name("MENU");
    else if(!strcmp(kind,"reload")) fault=1;
    else if(!strcmp(kind,"ticket")) fault=2;
    else if(!strcmp(kind,"core")) fault=3;
    else if(!strcmp(kind,"invalid-core")) { REG(TM_CART_META_OFFSET+4)=TM_CART_CAPACITY+1; fault=4; }
    else CHECK(0);
    CHECK(tm_backend_cart_source(&backend,&copy,&size,path)==-1);
    CHECK(!fault && !copy && !size && !*path);
    CHECK(REG(TM_CART_ACK_OFFSET)==previous_ack && !backend.cart_seen);
    printf("Cart copy lost %s: empty outputs and no staging ACK passed\n",kind);
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    CHECK(argc==1 || (argc==3 && !strcmp(argv[1],"--fault")));
    long page=sysconf(_SC_PAGESIZE); CHECK(page>0 && TM_REGION_BYTES%page==0);
    memory=mmap(NULL,TM_REGION_BYTES+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(memory!=MAP_FAILED && !mprotect(memory+TM_REGION_BYTES,page,PROT_NONE));
    char core_path[]="/tmp/tic80-copy-core-XXXXXX"; core_fd=mkstemp(core_path); CHECK(core_fd>=0);
    char source[256]; source[0]='/'; memset(source+1,'x',254); source[255]=0;
    memset(memory+TM_CART_DATA_OFFSET,0xff,TM_CART_CAPACITY);
    if(argc==3) lost(argv[2],core_path,source);
    else {
        const char *failures[]={"lost-entry","reload","ticket","core","invalid-core"};
        for(unsigned i=0;i<5;++i) lost(failures[i],core_path,source);
        const size_t sizes[]={4,4097,65535,TM_CART_CAPACITY-1,TM_CART_CAPACITY};
        for(unsigned item=0;item<5;++item) {
            uint64_t times[9]; size_t length=sizes[item];
            for(unsigned round=0;round<9;++round) {
                for(size_t i=0;i<length;++i) memory[TM_CART_DATA_OFFSET+i]=(uint8_t)(i*13+round);
                tm_backend backend; prepare(&backend,core_path,length,6,source);
                uint8_t *copy=NULL; size_t size=0; char path[256];
                uint64_t start=now_ns(); CHECK(tm_backend_cart_source(&backend,&copy,&size,path)==1); times[round]=now_ns()-start;
                CHECK(size==length && !strcmp(path,source) && REG(TM_CART_ACK_OFFSET)==6);
                memset(memory+TM_CART_DATA_OFFSET,0xcc,length); memset(memory+TM_CART_SOURCE_OFFSET+8,0xcc,256);
                for(size_t i=0;i<length;++i) CHECK(copy[i]==(uint8_t)(i*13+round));
                CHECK(!strcmp(path,source)); free(copy);
                CHECK(tm_backend_cart_source(&backend,&copy,&size,path)==0 && !copy && !size && !*path);
            }
            metric("backend",length,times,9);
        }
        tm_backend backend; prepare(&backend,core_path,TM_CART_CAPACITY+1,6,source);
        uint8_t *copy=NULL; size_t size=0; char path[256];
        CHECK(tm_backend_cart_source(&backend,&copy,&size,path)==2 && !copy && !size && !*path && REG(TM_CART_ACK_OFFSET)==6);
        // Exercise the second foreground copy with a valid maximum envelope.
        // Empty unknown chunks are permitted by the native cart format.
        char folder[]="/tmp/tic80-ipc-copy-XXXXXX"; CHECK(mkdtemp(folder));
        char file_path[256]; size_t prefix=strlen(folder);
        memcpy(file_path,folder,prefix); file_path[prefix++]='/';
        memset(file_path+prefix,'x',255-prefix); memcpy(file_path+251,".tic",4); file_path[255]=0;
        uint8_t *payload=calloc(1,TM_CART_CAPACITY); CHECK(payload);
        int file=open(file_path,O_WRONLY|O_CREAT|O_EXCL,0600); CHECK(file>=0);
        size_t written=0; while(written<TM_CART_CAPACITY) { ssize_t n=write(file,payload+written,TM_CART_CAPACITY-written); CHECK(n>0); written+=n; }
        CHECK(!close(file)); tm_studio_session *session=NULL;
        CHECK(tm_studio_session_open(&session,folder)==TM_STUDIO_OK);
        uint64_t times[5];
        for(unsigned round=0;round<5;++round) {
            memset(payload,0,TM_CART_CAPACITY); strcpy(source,file_path);
            defer_selection=1;
            uint64_t start=now_ns();
            CHECK(tm_studio_session_begin_select_source(session,payload,TM_CART_CAPACITY,"copy.tic",source,5000)==TM_STUDIO_OK);
            times[round]=now_ns()-start;
            CHECK(deferred_socket>=0 && !defer_selection);
            // Worker cannot read until after both caller-owned inputs change.
            memset(payload,0xff,TM_CART_CAPACITY); memset(source,'!',255);
            CHECK(__real_send(deferred_socket,&deferred_command,1,deferred_flags)==1); deferred_socket=-1;
            int result; do { result=tm_studio_session_poll(session,10); } while(result==TM_STUDIO_PENDING);
            CHECK(result==TM_STUDIO_CART_SELECTED && !strcmp(tm_studio_session_name(session)->path,file_path));
            CHECK(!*tm_studio_session_cart(session)->code.data);
        }
        metric("ipc",TM_CART_CAPACITY,times,5);
        CHECK(tm_studio_session_close(session)==TM_STUDIO_OK); free(payload);
        CHECK(!unlink(file_path) && !nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS));
        puts("Maximum cart copies: bounds, complete private payload/source before ACK, immediate reclamation, lost-session/ticket output clearing, rejected-transfer departure, and deferred IPC ownership passed; timing is simulated RAM, not native DDR qualification");
    }
    CHECK(!close(core_fd) && !unlink(core_path)); CHECK(!munmap(memory,TM_REGION_BYTES+page)); return 0;
}
