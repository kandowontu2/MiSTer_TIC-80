#define _GNU_SOURCE
#include "tic80_mister/history_private.h"
#include "tic.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"history I/O failed line %d: %s (%s)\n",__LINE__,#c,strerror(errno)); exit(1); } } while(0)
enum { COUNT=41, EDITS=256 };
static uint64_t write_calls,write_bytes;
static bool counting;
static unsigned fault;
static uint64_t fail_at=3;
ssize_t __real_write(int fd,const void *data,size_t size);
ssize_t __wrap_write(int fd,const void *data,size_t size)
{
    if(counting) ++write_calls;
    if(fault && write_calls==fail_at) { errno=fault==1?EINTR:EIO; return -1; }
    if(fault==1 && size>7919) size=7919;
    ssize_t result=__real_write(fd,data,size);
    if(counting && result>0) write_bytes+=result;
    return result;
}
typedef struct { u32 magic,version,count,request; uint64_t bytes; u32 reserved[2]; } bundle_header;
typedef struct { uint64_t offset,bytes; u32 size,reserved[3]; } bundle_index;
typedef struct { u32 magic,version,size,count,position,request; uint64_t bytes; } member_header;
static uint64_t now(void)
{ struct timespec t; CHECK(!clock_gettime(CLOCK_MONOTONIC,&t)); return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static int compare(const void *a,const void *b)
{ uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return (x>y)-(x<y); }
static unsigned descriptors(void)
{
    DIR *folder=opendir("/proc/self/fd"); CHECK(folder); unsigned count=0; struct dirent *entry;
    while((entry=readdir(folder))) if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) ++count;
    CHECK(!closedir(folder)); return count;
}
static u8 *expected_wire(History *histories[],u32 request,size_t *length,unsigned *nodes)
{
    member_header members[COUNT]={0}; bundle_index index[COUNT]={0};
    uint64_t total=sizeof(bundle_header)+sizeof index; *nodes=0;
    for(unsigned i=0;i<COUNT;++i) {
        History *h=histories[i]; tm_history_item *first=h->list; while(first->prev) first=first->prev;
        members[i]=(member_header){.magic=0x31485554,.version=1,.size=h->size,.request=request,.bytes=sizeof(member_header)+2ull*h->size};
        for(tm_history_item *p=first;p;p=p->next) {
            if(p==h->list) members[i].position=members[i].count;
            ++members[i].count; ++*nodes; members[i].bytes+=8ull+p->data.end-p->data.start;
        }
        index[i]=(bundle_index){.offset=total,.bytes=members[i].bytes,.size=h->size}; total+=members[i].bytes;
    }
    CHECK(total<=SIZE_MAX); *length=total; u8 *bytes=calloc(1,*length); CHECK(bytes);
    bundle_header header={.magic=0x32485554,.version=2,.count=COUNT,.request=request,.bytes=total};
    memcpy(bytes,&header,sizeof header); memcpy(bytes+sizeof header,index,sizeof index);
    for(unsigned i=0;i<COUNT;++i) {
        History *h=histories[i]; u8 *p=bytes+index[i].offset;
        memcpy(p,members+i,sizeof members[i]); p+=sizeof members[i];
        memcpy(p,h->data,h->size); p+=h->size; memcpy(p,h->state,h->size); p+=h->size;
        tm_history_item *first=h->list; while(first->prev) first=first->prev;
        for(tm_history_item *item=first;item;item=item->next) {
            u32 range[]={item->data.start,item->data.end}; memcpy(p,range,sizeof range); p+=sizeof range;
            size_t count=range[1]-range[0]; if(count) { memcpy(p,item->data.buffer,count); p+=count; }
        }
        CHECK(p==bytes+index[i].offset+index[i].bytes);
    }
    return bytes;
}
int main(void)
{
    CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
    u32 sizes[COUNT]={2*TIC_CODE_SIZE};
    for(unsigned i=0;i<8;++i) {
        sizes[1+5*i]=TIC_SPRITES*sizeof(tic_tile); sizes[2+5*i]=sizeof(tic_map);
        sizes[3+5*i]=sizeof(tic_samples); sizes[4+5*i]=sizeof(tic_waveforms); sizes[5+5*i]=sizeof(tic_music);
    }
    History *histories[COUNT]; u8 *data[COUNT];
    for(unsigned i=0;i<COUNT;++i) {
        data[i]=calloc(1,sizes[i]); CHECK(data[i]); histories[i]=history_create(data[i],sizes[i]); CHECK(histories[i]);
        for(unsigned step=0;step<EDITS;++step) {
            data[i][(step*53+i*29)%sizes[i]]^=(u8)(1+step%255); CHECK(history_add(histories[i]));
        }
        for(unsigned step=0;step<i%7;++step) history_undo(histories[i]);
    }
    size_t length; unsigned nodes; u8 *expected=expected_wire(histories,91,&length,&nodes);
    uint64_t times[7],total=0; int fd=-1;
    for(unsigned sample=0;sample<7;++sample) {
        write_calls=write_bytes=0; counting=true; uint64_t start=now();
        fd=tm_history_bundle_snapshot(histories,COUNT,91); times[sample]=now()-start; counting=false;
        CHECK(fd>=0 && write_bytes==length); total+=times[sample];
        struct stat info; CHECK(!fstat(fd,&info) && (uint64_t)info.st_size==length);
        u8 *actual=malloc(length); CHECK(actual); CHECK(pread(fd,actual,length,0)==(ssize_t)length);
        CHECK(!memcmp(actual,expected,length)); free(actual);
        CHECK(tm_history_bundle_valid(fd,sizes,COUNT,NULL));
        if(sample<6) CHECK(!close(fd));
    }
    qsort(times,7,sizeof *times,compare); struct rusage usage; CHECK(!getrusage(RUSAGE_SELF,&usage));
    printf("HISTORY_IO histories=%u edits_per_history=%u nodes=%u bytes=%zu writes=%llu mean_ms=%.3f p50_ms=%.3f max_ms=%.3f peak_rss_kib=%ld\n",
        COUNT,EDITS,nodes,length,(unsigned long long)write_calls,total/7000000.0,times[3]/1e6,times[6]/1e6,usage.ru_maxrss);
    uint64_t normal_calls=write_calls;
    for(unsigned action=1;action<=3;++action) {
        unsigned before=descriptors(); write_calls=write_bytes=0; fault=action==1?1:2;
        fail_at=action==3?normal_calls:3; counting=true;
        int partial=tm_history_bundle_snapshot(histories,COUNT,91); counting=false; fault=0;
        if(action==1) {
            CHECK(partial>=0 && write_bytes==length); u8 *actual=malloc(length); CHECK(actual);
            CHECK(pread(partial,actual,length,0)==(ssize_t)length && !memcmp(actual,expected,length)); free(actual); CHECK(!close(partial));
        } else CHECK(partial==-1 && errno==EIO);
        CHECK(descriptors()==before);
    }
    const bundle_index *index=(const void*)(expected+sizeof(bundle_header));
    write_calls=write_bytes=0; counting=true; fault=1; fail_at=3;
    int single=tm_history_snapshot(histories[0],91); counting=false; fault=0;
    CHECK(single>=0 && write_bytes==index[0].bytes);
    u8 *single_bytes=malloc(index[0].bytes); CHECK(single_bytes);
    CHECK(pread(single,single_bytes,index[0].bytes,0)==(ssize_t)index[0].bytes);
    CHECK(!memcmp(single_bytes,expected+index[0].offset,index[0].bytes)); free(single_bytes); CHECK(!close(single));
    unsigned before=descriptors(); write_calls=0; counting=true; fault=2;
    single=tm_history_snapshot(histories[0],91); counting=false; fault=0;
    CHECK(single==-1 && errno==EIO && descriptors()==before);
    puts("Studio history I/O: interrupted and short bundle/single writes preserve exact bytes; early and final I/O errors reject snapshots without leaking descriptors");
    History **slots[COUNT]; bool restore[COUNT];
    for(unsigned i=0;i<COUNT;++i) {
        history_delete(histories[i]); memset(data[i],0xe0,sizes[i]); histories[i]=history_create(data[i],sizes[i]); CHECK(histories[i]);
        slots[i]=histories+i; restore[i]=true;
    }
    uint64_t started=now(); CHECK(tm_history_bundle_restore(slots,restore,COUNT,fd));
    printf("HISTORY_RESTORE histories=%u nodes=%u elapsed_ms=%.3f\n",COUNT,nodes,(now()-started)/1e6);
    CHECK(!close(fd));
    for(unsigned i=0;i<COUNT;++i) {
        CHECK(!memcmp(data[i],expected+index[i].offset+sizeof(member_header),sizes[i]));
        for(unsigned step=0;step<EDITS;++step) history_redo(histories[i]);
        for(unsigned step=0;step<EDITS;++step) history_undo(histories[i]);
        for(unsigned byte=0;byte<sizes[i];++byte) CHECK(!data[i][byte]);
        history_delete(histories[i]); free(data[i]);
    }
    free(expected);
    puts("Studio history I/O: full-size 41 histories, all 10537 nodes, exact v2 bytes, undone positions and complete undo/redo passed");
    CHECK(normal_calls<nodes/4);
    puts("Studio history I/O: checkpoint writes remain below one per four undo nodes");
    return 0;
}
