#define _GNU_SOURCE
#include "tic80_mister/history_private.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define SNAPSHOT_MAGIC 0x31485554u
#define BUNDLE_MAGIC 0x32485554u
#define SNAPSHOT_MAX (512ull*1024*1024)
#define SNAPSHOT_SEALS (F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)
typedef struct { u32 magic,version,size,count,position,request; uint64_t bytes; } header;
_Static_assert(sizeof(header)==32,"History wire header changed");
typedef struct { u32 magic,version,count,request; uint64_t bytes; u32 reserved[2]; } bundle_header;
typedef struct { uint64_t offset,bytes; u32 size,reserved[3]; } bundle_index;
_Static_assert(sizeof(bundle_header)==32 && sizeof(bundle_index)==32,"History bundle header changed");
static bool write_all(int fd,const void *data,size_t size) {
    const u8 *bytes=data;
    while(size) { ssize_t n=write(fd,bytes,size); if(n<0 && errno==EINTR) continue; if(n<=0) return false; bytes+=n; size-=(size_t)n; }
    return true;
}
/* Thousands of small undo ranges otherwise produce two syscalls per node.
 * Keep memory bounded independently of snapshot length, sharing this buffer
 * across every member of a bundle. Large blocks can go directly to memfd. */
typedef struct { int fd; size_t used; u8 bytes[64*1024]; } writer;
static bool flush(writer *out) {
    if(!write_all(out->fd,out->bytes,out->used)) return false;
    out->used=0; return true;
}
static bool append(writer *out,const void *data,size_t size) {
    const u8 *bytes=data;
    if(size>=sizeof out->bytes) return flush(out) && write_all(out->fd,data,size);
    while(size) {
        if(out->used==sizeof out->bytes && !flush(out)) return false;
        size_t count=sizeof out->bytes-out->used; if(count>size) count=size;
        memcpy(out->bytes+out->used,bytes,count); out->used+=count; bytes+=count; size-=count;
    }
    return true;
}
static bool read_all(int fd,void *data,size_t size,uint64_t *offset,uint64_t limit) {
    if(*offset>limit || size>limit-*offset) return false;
    u8 *bytes=data;
    while(size) { ssize_t n=pread(fd,bytes,size,(off_t)*offset); if(n<0 && errno==EINTR) continue; if(n<=0) return false; bytes+=n; size-=(size_t)n; *offset+=(uint64_t)n; }
    return true;
}
static bool sealed_length(int fd,uint64_t *length) {
    struct stat info; int seals=fcntl(fd,F_GET_SEALS);
    if(seals<0 || (seals&SNAPSHOT_SEALS)!=SNAPSHOT_SEALS || fstat(fd,&info) || !S_ISREG(info.st_mode) ||
       info.st_size<(off_t)sizeof(header) || (uint64_t)info.st_size>SNAPSHOT_MAX) return false;
    *length=(uint64_t)info.st_size; return true;
}
static bool inspect_block(int fd,u32 size,uint64_t start,uint64_t length,header *out) {
    uint64_t offset=start;
    if(length<sizeof *out || !read_all(fd,out,sizeof *out,&offset,start+length)) return false;
    return out->magic==SNAPSHOT_MAGIC && out->version==1 && out->size==size && size &&
        out->count && out->position<out->count && out->bytes==length &&
        (uint64_t)sizeof(header)+2ull*size+8ull*out->count<=out->bytes;
}
static bool inspect(int fd,u32 size,header *out) {
    uint64_t length; return sealed_length(fd,&length) && inspect_block(fd,size,0,length,out);
}
bool tm_history_snapshot_valid(int fd,u32 size,u32 *request) {
    header value; if(!inspect(fd,size,&value)) return false;
    if(request) *request=value.request; return true;
}
static bool measure(const History *history,u32 request,header *out) {
    if(!history || !history->list || !history->state || !history->data || !history->size) return false;
    const tm_history_item *first=history->list;
    while(first->prev) first=first->prev;
    header value={.magic=SNAPSHOT_MAGIC,.version=1,.size=history->size,.request=request};
    uint64_t total=sizeof value+2ull*history->size;
    for(const tm_history_item *item=first;item;item=item->next) {
        if(item->data.start>item->data.end || item->data.end>history->size ||
           (!value.count && (item->data.start || item->data.end)) ||
           (value.count && (item->data.start==item->data.end || !item->data.buffer))) return false;
        if(item==history->list) value.position=value.count;
        if(value.count==UINT32_MAX) return false;
        ++value.count; total+=8ull+item->data.end-item->data.start;
        if(total>SNAPSHOT_MAX) return false;
    }
    value.bytes=total; *out=value; return true;
}
static bool write_history(writer *out,const History *history,const header *value) {
    bool valid=append(out,value,sizeof *value) && append(out,history->data,history->size) &&
        append(out,history->state,history->size);
    const tm_history_item *first=history->list; while(first->prev) first=first->prev;
    for(const tm_history_item *item=first;valid && item;item=item->next) {
        u32 range[2]={item->data.start,item->data.end};
        valid=append(out,range,sizeof range) && append(out,item->data.buffer,item->data.end-item->data.start);
    }
    return valid;
}
int tm_history_snapshot(const History *history,u32 request) {
    header value; if(!measure(history,request,&value)) return -1;
    int fd=memfd_create("tic80-code-history",MFD_CLOEXEC|MFD_ALLOW_SEALING); if(fd<0) return -1;
    writer out={.fd=fd}; bool valid=write_history(&out,history,&value) && flush(&out);
    if(valid) valid=!fcntl(fd,F_ADD_SEALS,SNAPSHOT_SEALS);
    if(!valid) { close(fd); return -1; }
    return fd;
}
static History *read_history(void *data,u32 size,int fd,uint64_t start,uint64_t length,u8 **current) {
    header value; if(!data || !inspect_block(fd,size,start,length,&value)) return NULL;
    History *history=calloc(1,sizeof *history); u8 *captured=current?malloc(size):NULL;
    if(!history || (current && !captured)) { free(history); free(captured); return NULL; }
    history->size=size; history->data=data; history->state=malloc(size);
    uint64_t offset=start+sizeof value,limit=start+length; tm_history_item *first=NULL,*last=NULL;
    bool valid=history->state!=NULL;
    if(current) valid=valid && read_all(fd,captured,size,&offset,limit);
    else offset+=size;
    valid=valid && read_all(fd,history->state,size,&offset,limit);
    for(u32 index=0;valid && index<value.count;++index) {
        u32 range[2]; valid=read_all(fd,range,sizeof range,&offset,limit) &&
            range[0]<=range[1] && range[1]<=size &&
            (index?range[0]<range[1]:!range[0]&&!range[1]);
        if(!valid) break;
        tm_history_item *item=calloc(1,sizeof *item); if(!item) { valid=false; break; }
        item->prev=last; if(last) last->next=item; else first=item; last=item;
        item->data.start=range[0]; item->data.end=range[1];
        size_t count=range[1]-range[0];
        if(count) { item->data.buffer=malloc(count); valid=item->data.buffer && read_all(fd,item->data.buffer,count,&offset,limit); }
        if(index==value.position) history->list=item;
    }
    if(!valid || offset!=limit || !history->list) {
        history->list=first;
        if(first) history_delete(history); else { free(history->state); free(history); }
        free(captured); return NULL;
    }
    if(current) *current=captured;
    tm_history_touch(history); return history;
}
History *tm_history_snapshot_restore(void *data,u32 size,int fd) {
    uint64_t length; u8 *current=NULL;
    if(!sealed_length(fd,&length)) return NULL;
    History *history=read_history(data,size,fd,0,length,&current);
    if(history) { memcpy(data,current,size); free(current); }
    return history;
}
static bool inspect_bundle(int fd,const u32 sizes[],u32 count,bundle_header *value,bundle_index entries[]) {
    uint64_t length,offset=0;
    if(!sizes || !count || count>TM_HISTORY_GROUP_MAX || !sealed_length(fd,&length) ||
       !read_all(fd,value,sizeof *value,&offset,length) || value->magic!=BUNDLE_MAGIC || value->version!=2 ||
       value->count!=count || value->bytes!=length || value->reserved[0] || value->reserved[1] ||
       !read_all(fd,entries,sizeof *entries*count,&offset,length)) return false;
    for(u32 i=0;i<count;++i) {
        const bundle_index *entry=entries+i;
        if(!sizes[i] || entry->size!=sizes[i] || entry->offset!=offset || entry->reserved[0] || entry->reserved[1] || entry->reserved[2] ||
           entry->bytes<sizeof(header)+2ull*sizes[i]+8 || offset>length || entry->bytes>length-offset) return false;
        offset+=entry->bytes;
    }
    return offset==length;
}
bool tm_history_bundle_valid(int fd,const u32 sizes[],u32 count,u32 *request) {
    bundle_header value; bundle_index entries[TM_HISTORY_GROUP_MAX];
    if(!inspect_bundle(fd,sizes,count,&value,entries)) return false;
    if(request) *request=value.request; return true;
}
int tm_history_bundle_snapshot(History *const histories[],u32 count,u32 request) {
    if(!histories || !count || count>TM_HISTORY_GROUP_MAX) return -1;
    header values[TM_HISTORY_GROUP_MAX]; bundle_index entries[TM_HISTORY_GROUP_MAX]={0};
    bundle_header value={.magic=BUNDLE_MAGIC,.version=2,.count=count,.request=request};
    uint64_t offset=sizeof value+sizeof *entries*count;
    for(u32 i=0;i<count;++i) {
        if(!measure(histories[i],request,values+i) || offset>SNAPSHOT_MAX || values[i].bytes>SNAPSHOT_MAX-offset) return -1;
        entries[i]=(bundle_index){.offset=offset,.bytes=values[i].bytes,.size=histories[i]->size};
        offset+=values[i].bytes;
    }
    value.bytes=offset;
    int fd=memfd_create("tic80-editor-history",MFD_CLOEXEC|MFD_ALLOW_SEALING); if(fd<0) return -1;
    writer out={.fd=fd};
    bool valid=append(&out,&value,sizeof value) && append(&out,entries,sizeof *entries*count);
    for(u32 i=0;valid && i<count;++i) valid=write_history(&out,histories[i],values+i);
    if(valid) valid=flush(&out);
    if(valid) valid=!fcntl(fd,F_ADD_SEALS,SNAPSHOT_SEALS);
    if(!valid) { close(fd); return -1; } return fd;
}
bool tm_history_bundle_restore(History **const slots[],const bool restore_current[],u32 count,int fd) {
    if(!slots || !restore_current || !count || count>TM_HISTORY_GROUP_MAX) return false;
    u32 sizes[TM_HISTORY_GROUP_MAX];
    for(u32 i=0;i<count;++i) {
        if(!slots[i] || !*slots[i]) return false;
        for(u32 j=0;j<i;++j) if(slots[i]==slots[j] || *slots[i]==*slots[j]) return false;
        sizes[i]=(*slots[i])->size;
    }
    bundle_header value; bundle_index entries[TM_HISTORY_GROUP_MAX];
    if(!inspect_bundle(fd,sizes,count,&value,entries)) return false;
    History *copies[TM_HISTORY_GROUP_MAX]={0}; u8 *current[TM_HISTORY_GROUP_MAX]={0}; bool valid=true;
    for(u32 i=0;valid && i<count;++i) {
        header member;
        valid=inspect_block(fd,sizes[i],entries[i].offset,entries[i].bytes,&member) && member.request==value.request;
        if(valid) { copies[i]=read_history((*slots[i])->data,sizes[i],fd,entries[i].offset,entries[i].bytes,restore_current[i]?current+i:NULL); valid=copies[i]!=NULL; }
    }
    for(u32 i=0;i<count;++i) {
        if(valid) {
            if(current[i]) memcpy((*slots[i])->data,current[i],sizes[i]);
            history_delete(*slots[i]); *slots[i]=copies[i];
        } else history_delete(copies[i]);
        free(current[i]);
    }
    return valid;
}
