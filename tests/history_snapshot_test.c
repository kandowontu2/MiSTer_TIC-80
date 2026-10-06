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
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"history failed line %d: %s (%s)\n",__LINE__,#c,strerror(errno)); exit(1); } } while(0)
#define SEALS (F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)
typedef struct { u32 magic,version,size,count,position,request; uint64_t bytes; } wire_header;
static int file(const void *bytes,size_t size,bool sealed)
{
    int fd=memfd_create("history-negative",MFD_ALLOW_SEALING|MFD_CLOEXEC); CHECK(fd>=0);
    CHECK(write(fd,bytes,size)==(ssize_t)size);
    if(sealed) CHECK(!fcntl(fd,F_ADD_SEALS,SEALS));
    return fd;
}
static void rejected(int fd,bool envelope)
{
    u8 data[32],prior[32]; memset(data,0xa5,sizeof data); memcpy(prior,data,sizeof data);
    if(envelope) CHECK(!tm_history_snapshot_valid(fd,sizeof data,NULL));
    CHECK(!tm_history_snapshot_restore(data,sizeof data,fd));
    CHECK(!memcmp(data,prior,sizeof data)); close(fd);
}
int main(void)
{
    u8 data[32]={0},initial[32]={0},first[32],second[32];
    History *history=history_create(data,sizeof data); CHECK(history);
    uint64_t revision=tm_history_revision(history); CHECK(revision);
    data[4]=1; CHECK(history_add(history)); memcpy(first,data,sizeof data);
    CHECK(tm_history_revision(history)!=revision); revision=tm_history_revision(history);
    data[27]=2; CHECK(history_add(history)); memcpy(second,data,sizeof data);
    history_undo(history); CHECK(!memcmp(data,first,sizeof data));
    CHECK(tm_history_revision(history)!=revision);
    int fd=tm_history_snapshot(history,19); CHECK(fd>=0);
    u32 request=0; CHECK(tm_history_snapshot_valid(fd,sizeof data,&request) && request==19);
    CHECK(fcntl(fd,F_GETFD)&FD_CLOEXEC);
    errno=0; CHECK(pwrite(fd,"x",1,0)==-1 && errno==EPERM);
    struct stat info; CHECK(!fstat(fd,&info));
    CHECK(ftruncate(fd,0)==-1 && errno==EPERM);
    CHECK(ftruncate(fd,info.st_size+1)==-1 && errno==EPERM);
    CHECK(fcntl(fd,F_ADD_SEALS,F_SEAL_FUTURE_WRITE)==-1 && errno==EPERM);
    /* Mutating and deleting the producer cannot affect its sealed snapshot. */
    data[3]=9; CHECK(history_add(history)); history_delete(history);
    u8 restored[32]; memset(restored,0x7c,sizeof restored);
    History *copy=tm_history_snapshot_restore(restored,sizeof restored,fd); CHECK(copy);
    CHECK(!memcmp(restored,first,sizeof data));
    history_redo(copy); CHECK(!memcmp(restored,second,sizeof data));
    history_undo(copy); CHECK(!memcmp(restored,first,sizeof data));
    history_undo(copy); CHECK(!memcmp(restored,initial,sizeof data));
    history_redo(copy); CHECK(!memcmp(restored,first,sizeof data));
    restored[9]=7; CHECK(history_add(copy)); memcpy(first,restored,sizeof data);
    history_redo(copy); CHECK(!memcmp(restored,first,sizeof data));
    history_delete(copy);
    /* Retain more than a small fixed undo window, including redo after capture. */
    u8 deep[32]={0},final[32],again[32]; History *long_history=history_create(deep,sizeof deep); CHECK(long_history);
    for(unsigned edit=0;edit<257;++edit) { ++deep[edit%32]; CHECK(history_add(long_history)); }
    memcpy(final,deep,sizeof final);
    for(unsigned edit=0;edit<100;++edit) history_undo(long_history);
    int long_fd=tm_history_snapshot(long_history,27); CHECK(long_fd>=0); history_delete(long_history);
    long_history=tm_history_snapshot_restore(again,sizeof again,long_fd); CHECK(long_history); close(long_fd);
    for(unsigned edit=0;edit<100;++edit) history_redo(long_history);
    CHECK(!memcmp(again,final,sizeof again));
    for(unsigned edit=0;edit<257;++edit) history_undo(long_history);
    CHECK(!memcmp(again,initial,sizeof again)); history_delete(long_history);
    size_t size=(size_t)info.st_size; u8 *bytes=malloc(size+1),*bad=malloc(size+1); CHECK(bytes&&bad);
    CHECK(pread(fd,bytes,size,0)==(ssize_t)size); close(fd);
    rejected(file(bytes,size,false),true);
    /* Envelope validation rejects corrupt metadata before committing an ACK. */
    for(unsigned fault=0;fault<7;++fault) {
        memcpy(bad,bytes,size); wire_header *header=(wire_header*)bad;
        switch(fault) {
        case 0: header->magic^=1; break;
        case 1: header->version++; break;
        case 2: header->size++; break;
        case 3: header->count=0; break;
        case 4: header->position=header->count; break;
        case 5: header->bytes--; break;
        case 6: header->count=UINT32_MAX; break;
        }
        rejected(file(bad,size,true),true);
    }
    rejected(file(bytes,7,true),true);
    /* Header-consistent malformed payloads must never alter caller data. */
    for(unsigned fault=0;fault<5;++fault) {
        memcpy(bad,bytes,size); wire_header *header=(wire_header*)bad;
        u32 *sentinel=(u32*)(bad+sizeof *header+64),*range=sentinel+2;
        size_t length=size;
        switch(fault) {
        case 0: sentinel[1]=1; break;
        case 1: range[0]=range[1]+1; break;
        case 2: range[1]=33; break;
        case 3: length--; header->bytes=length; break;
        case 4: bad[size]=0; length++; header->bytes=length; break;
        }
        rejected(file(bad,length,true),false);
    }
    fd=memfd_create("history-large",MFD_ALLOW_SEALING|MFD_CLOEXEC); CHECK(fd>=0);
    CHECK(!ftruncate(fd,512ull*1024*1024+1)); CHECK(!fcntl(fd,F_ADD_SEALS,SEALS)); rejected(fd,true);
    int pair[2]; CHECK(!pipe(pair)); close(pair[1]); rejected(pair[0],true);
    free(bad); free(bytes);
    puts("Studio history snapshots: sealed ownership, undo/redo branch, malformed envelope/ranges, truncation, trailing bytes and oversized/non-file descriptors passed");
    return 0;
}
