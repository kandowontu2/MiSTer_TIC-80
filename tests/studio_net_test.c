#define _GNU_SOURCE
#include "studio/net.h"
#include "studio/fs.h"
#include "studio/system.h"
#include "tic80_mister/studio.h"
#include "tic80_mister/cart_file.h"
#include "studio/studio.h"
#include "studio/config.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
typedef struct { int count,error; unsigned char *data; size_t size; tic_net *net; int action; } Result;
static void callback(const net_get_data *data)
{
    Result *result=data->calldata;
    CHECK(data->url);
    if(data->type==net_get_progress) { CHECK(data->progress.total>0); return; }
    ++result->count;
    if(data->type==net_get_error) result->error=data->error.code;
    else {
        free(result->data);
        result->size=data->done.size;
        result->data=malloc(result->size+1); CHECK(result->data);
        memcpy(result->data,data->done.data,result->size); result->data[result->size]=0;
    }
    if(result->action==1) { result->action=0; tic_net_get(result->net,"/binary",callback,result); }
    else if(result->action==2) tic_net_close(result->net);
}
static uint64_t clock_ns(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ULL+ts.tv_nsec; }
static void finish(tic_net *net,Result *result,int count)
{
    uint64_t deadline=clock_ns()+25000000000ULL;
    while(result->count<count) {
        CHECK(clock_ns()<deadline); tic_net_start(net); tic_net_end(net); usleep(1000);
    }
    CHECK(result->count==count);
}
static void check_binary(Result *result)
{
    static const unsigned char expected[]={'T','I','C',0,'\n','2','0','0',255};
    CHECK(!result->error&&result->size==sizeof expected);
    CHECK(!memcmp(result->data,expected,sizeof expected)); free(result->data); result->data=NULL;
}
static unsigned descriptors(void)
{
    DIR *dir=opendir("/proc/self/fd"); CHECK(dir); unsigned count=0;
    for(struct dirent *entry;(entry=readdir(dir));) if(entry->d_name[0]!='.') ++count;
    closedir(dir); return count;
}
static void no_children(void)
{ CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD); }
static int child_pid(void)
{
    /* MiSTer's kernel omits /proc/.../children; PPid is available in status. */
    DIR *dir=opendir("/proc"); CHECK(dir); int found=0;
    for(struct dirent *entry;(entry=readdir(dir));) {
        char *end; long pid=strtol(entry->d_name,&end,10);
        if(pid<=0||*end) continue;
        char path[128]; snprintf(path,sizeof path,"/proc/%ld/status",pid);
        FILE *file=fopen(path,"r"); if(!file) continue;
        char line[256];
        while(fgets(line,sizeof line,file))
            if(!strncmp(line,"PPid:",5)&&strtol(line+5,NULL,10)==getpid()) found=(int)pid;
        fclose(file); if(found) break;
    }
    closedir(dir); return found;
}
typedef struct { int done,files,folders; } Directory;
static bool item(const char *name,const char *title,const char *hash,s32 id,void *data,bool dir)
{
    Directory *result=data;
    if(dir) { CHECK(!strcmp(name,"games")); ++result->folders; }
    else { CHECK(!strcmp(name,"fixture.tic")&&!strcmp(title,"Network fixture")&&!strcmp(hash,"test")&&id==7); ++result->files; }
    return true;
}
static void dir_done(void *data) { ++((Directory *)data)->done; }
static void loaded(const u8 *bytes,s32 size,void *data)
{
    Result *r=data; ++r->count;
    if(!bytes||!size) { r->error=-1; return; }
    r->data=malloc(size+1); CHECK(r->data); memcpy(r->data,bytes,size); r->data[size]=0; r->size=size;
}
static int cleanup_file(const char *path,const struct stat *st,int kind,struct FTW *walk)
{ (void)st; (void)kind; (void)walk; return remove(path); }
static void filesystem(tic_net *net)
{
    char folder[]="/tmp/tic80-net-fs-XXXXXX"; CHECK(mkdtemp(folder));
    tic_fs *fs=tic_fs_create(folder,net); CHECK(fs);
    CHECK(tic_fs_makedir(fs,".local")==0); CHECK(tic_fs_makedir(fs,".local/cache")==0);
    tic_fs_changedir(fs,TIC_HOST); CHECK(tic_fs_ispubdir(fs));
    Directory dir={0}; tic_fs_enum(fs,item,dir_done,&dir);
    uint64_t deadline=clock_ns()+10000000000ULL;
    while(!dir.done) { CHECK(clock_ns()<deadline); tic_net_end(net); usleep(1000); }
    CHECK(dir.done==1&&dir.files==1&&dir.folders==1);
    Result cart={0}; tic_fs_hashload(fs,"fixture.tic","test",loaded,&cart); finish(net,&cart,1);
    CHECK(!cart.error&&cart.size>4&&cart.data[0]==5);
    char *args[]={"studio","--skip",NULL};
    Studio *studio=studio_create(2,args,48000,TIC80_PIXEL_COLOR_RGBA8888,folder,1,tic_layout_qwerty);
    CHECK(studio); tm_studio_bind(studio); studio_config_get(studio)->data.checkNewVersion=false;
    CHECK(tm_studio_hashload_apply(studio,cart.data,(s32)cart.size,"fixture.tic",NULL));
    CHECK(tm_studio_hashload_succeeded(studio));
    CHECK(!strcmp(getMemory(studio)->cart.code.data,"function TIC() cls(6) end\n"));
    CHECK(!tm_studio_hashload_apply(studio,NULL,0,"missing.tic",NULL));
    CHECK(!tm_studio_hashload_succeeded(studio));
    CHECK(!tm_studio_hashload_apply(studio,(u8 *)"broken",6,"bad.tic",NULL));
    CHECK(!strcmp(getMemory(studio)->cart.code.data,"function TIC() cls(6) end\n"));
    tm_studio_bind(NULL); studio_delete(studio); free(cart.data);
    Result cached={0}; tic_fs_hashload(fs,"fixture.tic","test",loaded,&cached);
    CHECK(cached.count==1&&!cached.error); free(cached.data);
    CHECK(tic_fs_saveroot(fs,".local/cache/test.tic","broken",6,true));
    Result repaired={0}; tic_fs_hashload(fs,"fixture.tic","test",loaded,&repaired);
    CHECK(!repaired.count); finish(net,&repaired,1); CHECK(!repaired.error); free(repaired.data);
    Result malformed={0}; tic_fs_hashload(fs,"malformed.tic","broken",loaded,&malformed);
    finish(net,&malformed,1); CHECK(malformed.error);
    s32 bad_size=0; CHECK(!tic_fs_loadroot(fs,".local/cache/broken.tic",&bad_size));
    Result failed={0}; tic_fs_hashload(fs,"missing.tic","missing",loaded,&failed);
    finish(net,&failed,1); CHECK(failed.error);
    tic_fs_homedir(fs);
    free(fs);
    /* Studio created config files beneath this private test root. */
    char cleanup_path[1024]; CHECK(snprintf(cleanup_path,sizeof cleanup_path,"%s",folder)<(int)sizeof cleanup_path);
    CHECK(nftw(cleanup_path,cleanup_file,16,FTW_DEPTH|FTW_PHYS)==0);
}
int main(int argc,char **argv)
{
    CHECK(argc>=2&&argc<=4);
    tic_net *net=tic_net_create(argv[1]); CHECK(net);
    if(argc>=3) {
        Result r={0}; tic_net_get(net,argv[2],callback,&r); finish(net,&r,1);
        CHECK(!r.error&&r.size>0); printf("HTTP GET %s: %zu bytes\n",argv[2],r.size);
        if(argc==4) {
            uint8_t *native=NULL; size_t native_size=0;
            CHECK(!tm_cart_file_decode(r.data,r.size,&native,&native_size)); free(native);
            FILE *file=fopen(argv[3],"wb"); CHECK(file);
            CHECK(fwrite(r.data,1,r.size,file)==r.size); CHECK(fclose(file)==0);
            puts("Downloaded cartridge passes bounded decoding");
        } else { CHECK(fwrite(r.data,1,r.size,stdout)==r.size); puts(""); }
        free(r.data); tic_net_close(net); no_children(); return 0;
    }
    unsigned before=descriptors();
    filesystem(net);
    Result binary={0}; tic_net_get(net,"/binary",callback,&binary); CHECK(!binary.count);
    finish(net,&binary,1); check_binary(&binary);
    Result redirect={0}; tic_net_get(net,"/redirect",callback,&redirect); finish(net,&redirect,1); check_binary(&redirect);
    Result encoded={0}; tic_net_get(net,"/name space/#%é.tic",callback,&encoded); finish(net,&encoded,1); check_binary(&encoded);
    Result missing={0}; tic_net_get(net,"/missing",callback,&missing); finish(net,&missing,1); CHECK(missing.error==404);
    Result empty={0}; tic_net_get(net,"/empty",callback,&empty); finish(net,&empty,1); CHECK(!empty.error&&empty.size==0); free(empty.data);
    Result too_big={0}; tic_net_get(net,"/oversize",callback,&too_big); finish(net,&too_big,1); CHECK(too_big.error);
    Result stream={0}; tic_net_get(net,"/stream-oversize",callback,&stream); finish(net,&stream,1); CHECK(stream.error==-EFBIG);
    Result truncated={0}; tic_net_get(net,"/truncated",callback,&truncated); finish(net,&truncated,1); CHECK(truncated.error);
    Result forbidden={0}; tic_net_get(net,"/file-redirect",callback,&forbidden); finish(net,&forbidden,1); CHECK(forbidden.error);
    Result timeout={0}; tic_net_get(net,"/timeout",callback,&timeout); finish(net,&timeout,1); CHECK(timeout.error==-ETIMEDOUT);
    Result bad={0}; tic_net_get(net,"file:///etc/passwd",callback,&bad); finish(net,&bad,1); CHECK(bad.error==-EINVAL);
    Result queued[17]={0};
    for(unsigned i=0;i<17;++i) tic_net_get(net,"/binary",callback,&queued[i]);
    CHECK(queued[16].count==1&&queued[16].error==-EAGAIN);
    for(unsigned i=0;i<16;++i) { finish(net,&queued[i],1); check_binary(&queued[i]); }
    Result chained={.net=net,.action=1}; tic_net_get(net,"/empty",callback,&chained);
    finish(net,&chained,2); check_binary(&chained);
    /* Keep non-CLOEXEC IPC-like descriptors open while curl is alive. */
    int ipc=open("/dev/null",O_RDWR); CHECK(ipc>=0); CHECK(dup2(ipc,3)==3); CHECK(dup2(ipc,4)==4);
    if(ipc>4) close(ipc);
    Result slow={0}; tic_net_get(net,"/slow",callback,&slow);
    uint64_t started=clock_ns(); tic_net_end(net); CHECK(clock_ns()-started<1000000000ULL);
    char path[128]; int pid=child_pid(); CHECK(pid>0);
    /* The spawn starts only the three standard descriptors; allow curl time to
     * create its own sockets, so compare inherited /dev/null targets. */
    for(unsigned fd=3;fd<=4;++fd) {
        snprintf(path,sizeof path,"/proc/%d/fd/%u",pid,fd);
        char target[128]; ssize_t n=readlink(path,target,sizeof target-1);
        if(n>=0) { target[n]=0; CHECK(strcmp(target,"/dev/null")); }
    }
    tic_net_close(net); CHECK(slow.count==1&&slow.error==-ECANCELED); no_children(); close(3); close(4);
    CHECK(descriptors()==before);
    net=tic_net_create(argv[1]); Result close_from_callback={.net=net,.action=2};
    tic_net_get(net,"/empty",callback,&close_from_callback); finish(net,&close_from_callback,1);
    free(close_from_callback.data); no_children(); CHECK(descriptors()==before);
    puts("Asynchronous HTTP binary/empty/redirect/status/limits/truncation, queue/reentrant callbacks, cancellation, descriptor isolation and cleanup passed");
    return 0;
}
