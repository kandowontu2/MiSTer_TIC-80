#define _GNU_SOURCE
#include "tic80_mister/main_launch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path,const char *data,size_t bytes)
{ FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(data,1,bytes,f)==bytes); assert(!fclose(f)); }
int main(void)
{
    assert(tm_main_mgl_initial_cart("<mistergamedescription><file delay='300' type='F' index='0' path='a&amp;b.tic'/></mistergamedescription>")==1);
    assert(tm_main_mgl_initial_cart("<!-- <file delay='0' type='F' index='0' path='fake'/> --><MisterGameDescription><FILE DELAY='0' TYPE='f' INDEX='0x40' PATH=''/></MisterGameDescription>")==1);
    assert(tm_main_mgl_initial_cart("<mistergamedescription><file type='F'/><file delay='0' type='F' index='00' path='a'/></mistergamedescription>")==1);
    assert(tm_main_mgl_initial_cart("<mistergamedescription><reset hold='2'/><file delay='0' type='F' index='0' path='a'/></mistergamedescription>")==1);
    assert(tm_main_mgl_initial_cart("<mistergamedescription><reset delay='2'/><file delay='0' type='F' index='0' path='a'/></mistergamedescription>")==0);
    assert(tm_main_mgl_initial_cart("<mistergamedescription><file delay='0' type='S' index='0' path='a'/><file delay='0' type='F' index='0' path='b'/></mistergamedescription>")==0);
    assert(tm_main_mgl_initial_cart("<mistergamedescription><file delay='0' type='F' index='1' path='a'/></mistergamedescription>")==0);
    assert(tm_main_mgl_initial_cart("<file delay='0' type='F' index='0' path='a'/>")==0);
    assert(tm_main_mgl_initial_cart(NULL)==-1);
    char root[]="/tmp/tic80-main-launch-XXXXXX"; assert(mkdtemp(root));
    char proc[512],exe[512],cmd[512],mgl[512];
    snprintf(proc,sizeof proc,"%s/123",root); assert(!mkdir(proc,0700));
    snprintf(exe,sizeof exe,"%s/123/exe",root); assert(!symlink("/media/fat/MiSTer",exe));
    snprintf(cmd,sizeof cmd,"%s/123/cmdline",root); snprintf(mgl,sizeof mgl,"%s/cart.MGL",root);
    const char xml[]="<mistergamedescription><file delay='300' type='F' index='0' path='cart.tic'/></mistergamedescription>";
    write_file(mgl,xml,strlen(xml));
    char argv[2048]; const char prefix[]="/media/fat/MiSTer\0/media/fat/TIC80.rbf\0";
    size_t n=sizeof prefix-1; memcpy(argv,prefix,n); strcpy(argv+n,mgl); n+=strlen(mgl)+1;
    write_file(cmd,argv,n); assert(tm_main_initial_cart(root)==1);
    /* argv[1] is the RBF; testing it for .mgl would miss the real contract. */
    write_file(cmd,prefix,sizeof prefix-1); assert(tm_main_initial_cart(root)==0);
    write_file(cmd,argv,n-1); assert(tm_main_initial_cart(root)==-1);
    write_file(cmd,argv,n); assert(!unlink(mgl)); assert(tm_main_initial_cart(root)==-1);
    assert(!unlink(exe)); assert(!symlink("/media/fat/OtherMain",exe)); assert(tm_main_initial_cart(root)==-1);
    assert(!unlink(exe)); assert(!symlink("/media/fat/MiSTer",exe));
    char duplicate[512],duplicate_exe[512],duplicate_cmd[512];
    snprintf(duplicate,sizeof duplicate,"%s/456",root); assert(!mkdir(duplicate,0700));
    snprintf(duplicate_exe,sizeof duplicate_exe,"%s/456/exe",root); assert(!symlink("/media/fat/MiSTer",duplicate_exe));
    snprintf(duplicate_cmd,sizeof duplicate_cmd,"%s/456/cmdline",root); write_file(duplicate_cmd,prefix,sizeof prefix-1);
    assert(tm_main_initial_cart(root)==-1);
    assert(!unlink(duplicate_cmd)); assert(!unlink(duplicate_exe)); assert(!rmdir(duplicate));
    assert(!unlink(cmd)); assert(!unlink(exe)); assert(!rmdir(proc)); assert(!rmdir(root));
    puts("Stock Main launch: real argv[2], pinned first-action parsing, bounded/ambiguous context rejection passed");
}
