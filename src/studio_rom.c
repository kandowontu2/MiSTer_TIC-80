#define _GNU_SOURCE
#include "tic80_mister/studio.h"
#include "tic80_mister/cart_file.h"
#include "studio/studio.h"
#include "cart.h"
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

bool tm_studio_cart_destination(Studio *studio,const char *name,bool use_source,char out[TICNAME_MAX])
{
    if(!name || !*name) return false;
    const CartName *cart=studioCart(studio);
    if(use_source && *cart->path) {
        const char *slash=strrchr(cart->path,'/');
        const char *base=strrchr(name,'/'); base=base?base+1:name;
        int length=snprintf(out,TICNAME_MAX,"%.*s%s",slash?(int)(slash-cart->path+1):0,cart->path,base);
        return length>0 && length<TICNAME_MAX;
    }
    char root[TICNAME_MAX],cwd[TICNAME_MAX];
    strcpy(root,tic_fs_pathroot(studio_fs(studio),""));
    tic_fs_dir(studio_fs(studio),cwd);
    int length;
    if(*name=='/') length=snprintf(out,TICNAME_MAX,"%s%s",root,name+1);
    else length=snprintf(out,TICNAME_MAX,"%s%s%s%s",root,cwd,*cwd?"/":"",name);
    return length>0 && length<TICNAME_MAX;
}

bool tm_studio_cart_file_apply(Studio *studio,const char *path)
{
    char source[PATH_MAX];
    if(!path || !realpath(path,source) || strlen(source)>=TICNAME_MAX) return false;
    int fd=open(source,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0) return false;
    struct stat before,after;
    bool valid=!fstat(fd,&before) && S_ISREG(before.st_mode) && before.st_size>=4 && before.st_size<=4*1024*1024;
    u8 *bytes=valid?malloc((size_t)before.st_size):NULL;
    valid=valid && bytes;
    size_t count=0;
    while(valid && count<(size_t)before.st_size) {
        ssize_t got=read(fd,bytes+count,(size_t)before.st_size-count);
        if(got<0 && errno==EINTR) continue;
        if(got<=0) { valid=false; break; }
        count+=(size_t)got;
    }
    if(valid) valid=!fstat(fd,&after) && before.st_size==after.st_size &&
        before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec;
    if(close(fd)) valid=false;
    const char *name=strrchr(source,'/')+1;
    if(valid) valid=tm_studio_hashload_apply(studio,bytes,(s32)count,name,NULL);
    if(valid) studioSetCartName(studio,name,source);
    free(bytes);
    return valid;
}

bool tm_studio_cart_source(const char *path,const u8 *bytes,size_t size,char out[TICNAME_MAX])
{
    out[0]=0;
    char source[PATH_MAX];
    if(!path || *path!='/' || !bytes || !size || size>4*1024*1024 ||
        !realpath(path,source) || strlen(source)>=TICNAME_MAX) return false;
    int fd=open(source,O_RDONLY|O_NONBLOCK|O_CLOEXEC); if(fd<0) return false;
    struct stat before,after;
    bool valid=!fstat(fd,&before) && S_ISREG(before.st_mode) && before.st_size==(off_t)size;
    u8 block[4096]; size_t count=0;
    while(valid && count<size) {
        size_t want=size-count; if(want>sizeof block) want=sizeof block;
        ssize_t got=read(fd,block,want);
        if(got<0 && errno==EINTR) continue;
        if(got<=0 || memcmp(block,bytes+count,(size_t)got)) { valid=false; break; }
        count+=(size_t)got;
    }
    if(valid) valid=!fstat(fd,&after) && before.st_size==after.st_size &&
        before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec;
    if(close(fd)) valid=false;
    if(valid) strcpy(out,source);
    return valid;
}

enum { CART_WRITE_ERROR, CART_WRITE_OK, CART_WRITE_OCCUPIED, CART_WRITE_PUBLISHED_ERROR };
static tm_studio_cart_publish_guard publish_guard;
static void *publish_data;
static tm_studio_cart_temp_guard temp_guard;
static void *temp_data;
void tm_studio_cart_publish_hook(tm_studio_cart_publish_guard guard,void *data)
{ publish_guard=guard; publish_data=data; }
void tm_studio_cart_temp_hook(tm_studio_cart_temp_guard guard,void *data)
{ temp_guard=guard; temp_data=data; }
// Publication and durability are separate: a committed name must survive a
// reported sync/close failure, while the editor still retains its unsaved hash.
static int cart_write(Studio *studio,const char *path,const void *bytes,s32 size,bool replace)
{
    if(!path || !*path || !bytes || size<=0 || strlen(path)>=TICNAME_MAX) return false;
    char target[PATH_MAX],temporary[PATH_MAX],directory[PATH_MAX];
    struct stat info;
    bool existing=!lstat(path,&info);
    if(!replace && existing) return CART_WRITE_OCCUPIED;
    if(existing) {
        // Follow an existing file alias, retaining the alias itself. Reject
        // special files and broken links before creating a replacement.
        if(!realpath(path,target) || stat(target,&info) || !S_ISREG(info.st_mode)) return false;
    } else {
        if(errno!=ENOENT) return false;
        strcpy(target,path);
    }
    strcpy(directory,target);
    char *slash=strrchr(directory,'/');
    if(slash) { if(slash==directory) slash[1]=0; else *slash=0; }
    else strcpy(directory,".");
    if(snprintf(temporary,sizeof temporary,"%s/.tic80-cart-XXXXXX",directory)>=(int)sizeof temporary) return false;
    int dir=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir<0) return false;
    int fd=mkstemp(temporary);
    bool valid=fd>=0;
    struct stat owned;
    bool ownership=valid && !fstat(fd,&owned) && S_ISREG(owned.st_mode);
    valid=valid && ownership;
    if(valid && studio && temp_guard && !temp_guard(temporary,fd,temp_data)) valid=false;
    if(valid && existing && fchmod(fd,info.st_mode&0777)) valid=false;
    size_t count=0;
    while(valid && count<(size_t)size) {
        ssize_t written=write(fd,(const u8*)bytes+count,(size_t)size-count);
        if(written<0 && errno==EINTR) continue;
        if(written<=0) { valid=false; break; }
        count+=(size_t)written;
    }
    if(valid && fsync(fd)) valid=false;
    if(valid && studio && publish_guard && !publish_guard(studio,path,target,fd,bytes,size,publish_data)) valid=false;
    if(fd>=0 && close(fd)) valid=false;
    bool occupied=false,published=false;
    if(valid) {
        if(replace ? rename(temporary,target) : renameat2(AT_FDCWD,temporary,AT_FDCWD,target,RENAME_NOREPLACE)) {
            occupied=!replace && errno==EEXIST;
            valid=false;
        } else published=true;
    }
    // A directory sync failure is reported even though rename may already
    // have committed. The editor retains its modified state for a retry.
    if(valid && fsync(dir)) valid=false;
    if(close(dir)) { valid=false; occupied=false; }
    // Recheck ownership before cleaning a failed write's temporary name.
    struct stat leftover;
    if(!valid && ownership && !lstat(temporary,&leftover) && S_ISREG(leftover.st_mode) &&
       leftover.st_dev==owned.st_dev && leftover.st_ino==owned.st_ino) unlink(temporary);
    return valid ? CART_WRITE_OK : published ? CART_WRITE_PUBLISHED_ERROR :
        occupied ? CART_WRITE_OCCUPIED : CART_WRITE_ERROR;
}

bool tm_studio_cart_write(const char *path,const void *bytes,s32 size)
{ return cart_write(NULL,path,bytes,size,true)==CART_WRITE_OK; }

bool tm_studio_cart_save(Studio *studio,const char *name,bool use_source,
                        const void *bytes,s32 size,char out[TICNAME_MAX])
{
    if(!tm_studio_cart_destination(studio,name,use_source,out)) return false;
    if(!use_source || *studioCart(studio)->path) {
        int result=cart_write(studio,out,bytes,size,true);
        if(result==CART_WRITE_OK || result==CART_WRITE_PUBLISHED_ERROR)
            studioSetCartName(studio,strrchr(out,'/')?strrchr(out,'/')+1:out,out);
        return result==CART_WRITE_OK;
    }
    char original[TICNAME_MAX]; strcpy(original,out);
    const char *base=strrchr(original,'/'); base=base?base+1:original;
    const char *extension=strrchr(base,'.'); if(!extension) extension=original+strlen(original);
    for(unsigned index=1;index<=9999;++index) {
        if(index>1 && snprintf(out,TICNAME_MAX,"%.*s-%u%s",(int)(extension-original),original,index,extension)>=TICNAME_MAX) return false;
        int result=cart_write(studio,out,bytes,size,false);
        if(result==CART_WRITE_OK || result==CART_WRITE_PUBLISHED_ERROR)
            studioSetCartName(studio,strrchr(out,'/')?strrchr(out,'/')+1:out,out);
        if(result==CART_WRITE_OK) return true;
        if(result!=CART_WRITE_OCCUPIED) return false;
    }
    return false;
}

bool tm_studio_cart_data_valid(const u8 *bytes,s32 size)
{
    uint8_t *native=NULL; size_t native_size=0;
    bool valid=bytes&&size>0&&!tm_cart_file_decode(bytes,(size_t)size,&native,&native_size);
    free(native); return valid;
}

bool tm_studio_hashload_apply(Studio *studio,const u8 *bytes,s32 size,
                              const char *name,const char *section)
{
    uint8_t *native=NULL; size_t native_size=0;
    bool success=false;
    if(bytes&&size>0&&!tm_cart_file_decode(bytes,(size_t)size,&native,&native_size)) {
        tic_cartridge *cart=calloc(1,sizeof *cart);
        if(cart) {
            tic_cart_load(cart,native,(s32)native_size);
            loadCartSection(studio,cart,section);
            tic_api_reset(getMemory(studio));
            if(!section) studioSetCartName(studio,name,tic_fs_path(studio_fs(studio),name));
            studioRomLoaded(studio);
            free(cart); success=true;
        }
    }
    free(native);
    tm_studio_hashload_result(studio,success);
    if(!success) tm_studio_popup(studio,"Cart download failed");
    return success;
}
