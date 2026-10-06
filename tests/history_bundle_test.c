#define _GNU_SOURCE
#include "tic80_mister/history_private.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"bundle failed line %d: %s (%s)\n",__LINE__,#c,strerror(errno)); exit(1); } } while(0)
#define COUNT 41
#define SEALS (F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)
typedef struct { u32 magic,version,count,request; uint64_t bytes; u32 reserved[2]; } wire_header;
typedef struct { uint64_t offset,bytes; u32 size,reserved[3]; } wire_index;
static u32 sizes[COUNT];
static int file(const void *bytes,size_t size,bool sealed)
{
    int fd=memfd_create("bundle-negative",MFD_ALLOW_SEALING|MFD_CLOEXEC); CHECK(fd>=0);
    CHECK(write(fd,bytes,size)==(ssize_t)size); if(sealed) CHECK(!fcntl(fd,F_ADD_SEALS,SEALS)); return fd;
}
static void rejected(int fd,bool envelope)
{
    History *histories[COUNT],*prior[COUNT],**slots[COUNT]; u8 data[COUNT][64],before[COUNT][64];
    bool restore[COUNT]={true};
    memset(data,0xcc,sizeof data); memcpy(before,data,sizeof data);
    for(unsigned i=0;i<COUNT;++i) { histories[i]=prior[i]=history_create(data[i],sizes[i]); CHECK(histories[i]); slots[i]=histories+i; }
    if(envelope) CHECK(!tm_history_bundle_valid(fd,sizes,COUNT,NULL));
    CHECK(!tm_history_bundle_restore(slots,restore,COUNT,fd));
    CHECK(!memcmp(data,before,sizeof data));
    for(unsigned i=0;i<COUNT;++i) { CHECK(histories[i]==prior[i]); CHECK(!histories[i]->list->prev && !histories[i]->list->next); history_delete(histories[i]); }
    close(fd);
}
int main(void)
{
    History *histories[COUNT],**slots[COUNT]; u8 data[COUNT][64]={0},first[COUNT][64],second[COUNT][64],captured[COUNT][64];
    for(unsigned i=0;i<COUNT;++i) {
        sizes[i]=32+i%17; histories[i]=history_create(data[i],sizes[i]); CHECK(histories[i]);
        data[i][0]=i+1; CHECK(history_add(histories[i])); memcpy(first[i],data[i],64);
        data[i][sizes[i]-1]=2*(i+1); CHECK(history_add(histories[i])); memcpy(second[i],data[i],64);
        if(i&1) history_undo(histories[i]); memcpy(captured[i],data[i],64);
    }
    int fd=tm_history_bundle_snapshot(histories,COUNT,77); CHECK(fd>=0); u32 request=0;
    CHECK(tm_history_bundle_valid(fd,sizes,COUNT,&request) && request==77);
    CHECK(fcntl(fd,F_GETFD)&FD_CLOEXEC); errno=0; CHECK(pwrite(fd,"x",1,0)==-1 && errno==EPERM);
    struct stat info; CHECK(!fstat(fd,&info)); size_t size=info.st_size;
    u8 *bytes=malloc(size+1),*bad=malloc(size+1); CHECK(bytes&&bad); CHECK(pread(fd,bytes,size,0)==(ssize_t)size);
    /* Replacing the producer cannot modify the immutable snapshot. The new
     * asset buffers model acknowledged RUN changes absent from undo history. */
    for(unsigned i=0;i<COUNT;++i) { history_delete(histories[i]); memset(data[i],0xe0,64); histories[i]=history_create(data[i],sizes[i]); slots[i]=histories+i; }
    const bool restore_current[COUNT]={true};
    CHECK(tm_history_bundle_restore(slots,restore_current,COUNT,fd)); close(fd);
    for(unsigned i=0;i<COUNT;++i) {
        if(!i) CHECK(!memcmp(data[i],captured[i],sizes[i]));
        else for(unsigned j=0;j<64;++j) CHECK(data[i][j]==0xe0);
        history_undo(histories[i]);
        if(i&1) for(unsigned j=0;j<sizes[i];++j) CHECK(!data[i][j]);
        else CHECK(!memcmp(data[i],first[i],sizes[i]));
        history_redo(histories[i]); CHECK(!memcmp(data[i],captured[i],sizes[i]));
        history_redo(histories[i]); CHECK(!memcmp(data[i],second[i],sizes[i]));
        history_undo(histories[i]); history_undo(histories[i]);
        for(unsigned j=0;j<sizes[i];++j) CHECK(!data[i][j]);
        history_delete(histories[i]);
    }
    rejected(file(bytes,size,false),true);
    for(unsigned fault=0;fault<9;++fault) {
        memcpy(bad,bytes,size); wire_header *header=(wire_header*)bad; wire_index *entries=(wire_index*)(bad+sizeof *header);
        switch(fault) {
        case 0: header->magic^=1; break;
        case 1: header->version++; break;
        case 2: header->count--; break;
        case 3: header->reserved[0]=1; break;
        case 4: header->bytes--; break;
        case 5: entries[1].offset=entries[0].offset; break;
        case 6: entries[COUNT-1].bytes++; break;
        case 7: entries[7].size++; break;
        case 8: entries[COUNT-1].reserved[2]=1; break;
        }
        rejected(file(bad,size,true),true);
    }
    /* A malformed final history must not commit any of the earlier 40 parsed
     * histories, private code state, or acknowledged cartridge assets. */
    for(unsigned fault=0;fault<5;++fault) {
        memcpy(bad,bytes,size); wire_index *entries=(wire_index*)(bad+sizeof(wire_header));
        u8 *member=bad+entries[COUNT-1].offset; u32 *header=(u32*)member;
        u32 *sentinel=(u32*)(member+32+2*sizes[COUNT-1]);
        switch(fault) {
        case 0: header[5]++; break;
        case 1: header[4]=header[3]; break;
        case 2: sentinel[1]=1; break;
        case 3: sentinel[2]=sentinel[3]+1; break;
        case 4: sentinel[3]=sizes[COUNT-1]+1; break;
        }
        rejected(file(bad,size,true),false);
    }
    rejected(file(bytes,7,true),true);
    memcpy(bad,bytes,size); bad[size]=0; ((wire_header*)bad)->bytes=size+1; rejected(file(bad,size+1,true),true);
    fd=memfd_create("bundle-large",MFD_ALLOW_SEALING|MFD_CLOEXEC); CHECK(fd>=0);
    CHECK(!ftruncate(fd,512ull*1024*1024+1)); CHECK(!fcntl(fd,F_ADD_SEALS,SEALS)); rejected(fd,true);
    free(bad); free(bytes);
    puts("Studio history bundle: all 41 chains/positions restored, acknowledged asset data retained, malformed table/final member rejected atomically and descriptors sealed");
    return 0;
}
