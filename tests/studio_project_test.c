#define _GNU_SOURCE
#include "tic80_mister/studio_session.h"
#include "tic80_mister/cart_file.h"
#include "studio/studio.h"
#include "studio/config.h"
#include "cart.h"
#include "tools.h"
#include "studio/rom.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s errno=%d\n",__LINE__,#c,errno); exit(1); } } while(0)
static int fault,short_write,directory_sync_failures;
static int temporary_fd(int fd)
{
    char proc[64],path[4096]; snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
    ssize_t count=readlink(proc,path,sizeof path-1); if(count<0) return 0;
    path[count]=0; return strstr(path,"/.tic80-cart-")!=NULL;
}
ssize_t __real_write(int,const void*,size_t);
int __real_fsync(int);
int __real_rename(const char*,const char*);
int __real_renameat2(int,const char*,int,const char*,unsigned);
static void write_cart(const char *path,const char *code);
static int publication_race,publication_failure;
static void published_fault(const char *to,bool before)
{
    const char *marker=getenv("TM_PROJECT_PUBLISH_MARKER");
    FILE *file=marker?fopen(marker,"rb"):NULL;
    if(!file) return;
    int action=fgetc(file); CHECK(!fclose(file));
    if(before!=(action=='B')) return;
    CHECK(!unlink(marker));
    if(action=='H') { struct timespec delay={2,0}; nanosleep(&delay,NULL); }
    if(action=='E') {
        FILE *published=fopen(to,"r+b"); CHECK(published);
        struct stat before,after; CHECK(!fstat(fileno(published),&before));
        u8 *bytes=malloc(before.st_size); CHECK(bytes);
        CHECK(fread(bytes,1,before.st_size,published)==(size_t)before.st_size);
        u8 *code=memmem(bytes,before.st_size,"cls(",4); CHECK(code);
        CHECK(!fseek(published,(long)(code+4-bytes),SEEK_SET) && fputc('3',published)!=EOF && !fflush(published));
        CHECK(!fstat(fileno(published),&after) && before.st_dev==after.st_dev &&
            before.st_ino==after.st_ino && before.st_size==after.st_size);
        CHECK(!fclose(published)); free(bytes);
    }
    if(action=='R') { CHECK(!unlink(to)); write_cart(to,"function TIC() cls(3) end\n"); }
    if(action=='F') { CHECK(!unlink(to) && !mkfifo(to,0600)); }
    kill(getpid(),SIGKILL);
}
int __wrap_renameat2(int fromdir,const char *from,int todir,const char *to,unsigned flags)
{
    if(publication_failure) { errno=EOPNOTSUPP; return -1; }
    if(publication_race) {
        publication_race=0;
        write_cart(to,"function TIC() cls(3) end\n");
    }
    published_fault(to,true);
    int result=__real_renameat2(fromdir,from,todir,to,flags);
    if(!result) published_fault(to,false);
    return result;
}
ssize_t __wrap_write(int fd,const void *data,size_t size)
{
    if(temporary_fd(fd)) {
        const char *temp_marker=getenv("TM_PROJECT_TEMP_MARKER");
        FILE *trigger=temp_marker?fopen(temp_marker,"rb"):NULL;
        if(trigger) {
            int action=fgetc(trigger); CHECK(!fclose(trigger) && !unlink(temp_marker));
            char proc[64],temporary[4096]; snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
            ssize_t count=readlink(proc,temporary,sizeof temporary-1); CHECK(count>0); temporary[count]=0;
            const char *trace=getenv("TM_PROJECT_TEMP_TRACE"); CHECK(trace);
            FILE *record=fopen(trace,"wb"); CHECK(record && fputs(temporary,record)>=0 && !fclose(record));
            if(action=='P') CHECK(__real_write(fd,data,size>3?3:size)>0);
            if(action=='R') { CHECK(!unlink(temporary)); write_cart(temporary,"function TIC() cls(3) end\n"); }
            if(action=='L') { const char *target=getenv("TM_PROJECT_TEMP_LINK"); CHECK(target && !unlink(temporary) && !symlink(target,temporary)); }
            if(action=='F') CHECK(!unlink(temporary) && !mkfifo(temporary,0600));
            kill(getpid(),SIGKILL);
        }
        const char *marker=getenv("TM_PROJECT_STALL_MARKER");
        if(marker && !access(marker,F_OK)) {
            const char *trace=getenv("TM_PROJECT_TEMP_TRACE"); CHECK(trace);
            char proc[64],temporary[4096]; snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
            ssize_t count=readlink(proc,temporary,sizeof temporary-1); CHECK(count>0); temporary[count]=0;
            FILE *record=fopen(trace,"wb"); CHECK(record && fputs(temporary,record)>=0 && !fclose(record));
            struct timespec delay={2,0}; nanosleep(&delay,NULL);
        }
        if(fault==1) {
            if(!short_write++) return __real_write(fd,data,size>3?3:size);
            errno=EIO; return -1;
        }
    }
    return __real_write(fd,data,size);
}
int __wrap_fsync(int fd)
{
    struct stat info;
    if(!fstat(fd,&info) && S_ISDIR(info.st_mode)) {
        const char *marker=getenv("TM_PROJECT_DIRSYNC_MARKER");
        if(directory_sync_failures || (marker && !access(marker,F_OK))) {
            if(directory_sync_failures) --directory_sync_failures;
            if(marker) unlink(marker);
            errno=EIO; return -1;
        }
    }
    if(fault==2 && temporary_fd(fd)) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
int __wrap_rename(const char *from,const char *to)
{
    if(fault==3 && strstr(from,"/.tic80-cart-")) { errno=EIO; return -1; }
    if(strstr(from,"/.tic80-cart-")) published_fault(to,true);
    int result=__real_rename(from,to);
    if(!result && strstr(from,"/.tic80-cart-")) published_fault(to,false);
    return result;
}
static void write_cart(const char *path,const char *code)
{
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); strcpy(cart->code.data,code);
    cart->banks[7].map.data[33]=77;
    u8 *bytes=malloc(2*sizeof *cart); CHECK(bytes); s32 size=tic_cart_save(cart,bytes);
    FILE *file=fopen(path,"wb"); CHECK(file && fwrite(bytes,1,size,file)==(size_t)size && !fclose(file));
    free(bytes); free(cart);
}
static void check_cart(const char *path,const char *code,int png)
{
    FILE *file=fopen(path,"rb"); CHECK(file); CHECK(!fseek(file,0,SEEK_END));
    long size=ftell(file); CHECK(size>0); rewind(file); u8 *bytes=malloc(size); CHECK(bytes);
    CHECK(fread(bytes,1,size,file)==(size_t)size && !fclose(file));
    CHECK(!png || (size>8 && !memcmp(bytes,"\x89PNG\r\n\x1a\n",8)));
    u8 *native=NULL; size_t count=0; CHECK(!tm_cart_file_decode(bytes,size,&native,&count));
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); tic_cart_load(cart,native,count);
    CHECK(!strcmp(cart->code.data,code) && cart->banks[7].map.data[33]==77);
    free(cart); free(native); free(bytes);
}
static unsigned long frames;
static int tick(tm_studio_session *s,tic80_input input,unsigned ms)
{ return tm_studio_session_tick(s,input,frames++*1000000000ULL/60,ms); }
static void idle(tm_studio_session *s,unsigned count)
{ while(count--) CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_OK); }
static int key(tm_studio_session *s,tic_key code,tic_key modifier,unsigned ms)
{
    tic80_input input={0}; input.keyboard.keys[0]=code; input.keyboard.keys[1]=modifier;
    int result=tick(s,input,ms); idle(s,1); return result;
}
static int remove_entry(const char *path,const struct stat *info,int kind,struct FTW *walk)
{ (void)info; (void)kind; (void)walk; return remove(path); }
static unsigned temporary_count(const char *folder)
{
    DIR *dir=opendir(folder); CHECK(dir); unsigned count=0; struct dirent *entry;
    while((entry=readdir(dir))) if(!strncmp(entry->d_name,".tic80-cart-",12)) ++count;
    CHECK(!closedir(dir)); return count;
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--studio-worker")) return tm_studio_session_worker(argc,argv);
    CHECK(argc==1);
    CHECK(!tic_tool_has_ext("", ".tic") && !tic_tool_has_ext("x", ".png"));
    CHECK(tic_tool_has_ext(".tic", ".tic") && tic_tool_has_ext("GAME.TiC", ".tic"));
    CHECK(tic_tool_has_ext("art.PNG", ".png") && !tic_tool_has_ext("a.tic.backup", ".tic"));
    CHECK(!strcmp(getCartName("x"), "x.tic"));
    CHECK(!strcmp(getCartName(".tic"), ".tic"));
    CHECK(!strcmp(getCartName("a.tic.b.TIC"), "a.tic.b.TIC"));
    char long_name[TICNAME_MAX+16]; memset(long_name, 'x', sizeof long_name-1); long_name[sizeof long_name-1]=0;
    CHECK(!strcmp(getCartName(long_name), long_name));
    char folder[]="/tmp/tic80-studio-project-XXXXXX"; CHECK(mkdtemp(folder));
    char nested[1024],other[1024],path[1024],alias[1024],png[1024],bad[1024],fifo[1024],marker[1024];
    snprintf(nested,sizeof nested,"%s/nested",folder); CHECK(!mkdir(nested,0700));
    snprintf(other,sizeof other,"%s/other",folder); CHECK(!mkdir(other,0700));
    snprintf(path,sizeof path,"%s/nested/cart space.tic",folder);
    snprintf(alias,sizeof alias,"%s/alias.tic",folder); CHECK(!symlink(path,alias));
    snprintf(png,sizeof png,"%s/other/art.png",folder);
    snprintf(bad,sizeof bad,"%s/bad.tic",folder); FILE *file=fopen(bad,"wb"); CHECK(file); CHECK(fputs("junk",file)>=0 && !fclose(file));
    snprintf(fifo,sizeof fifo,"%s/pipe.tic",folder); CHECK(!mkfifo(fifo,0600));
    snprintf(marker,sizeof marker,"%s/stall",folder); CHECK(!setenv("TM_PROJECT_STALL_MARKER",marker,1));
    char publish_marker[1024]; snprintf(publish_marker,sizeof publish_marker,"%s/publish-fault",folder);
    CHECK(!setenv("TM_PROJECT_PUBLISH_MARKER",publish_marker,1));
    const char *original="function TIC() cls(6) end\n",*edited="function TIC() cls(12) end\n";
    write_cart(path,original);
    char decoy[1024]; snprintf(decoy,sizeof decoy,"%s/.tic80-cart-kept00",nested);
    write_cart(decoy,original);
    char temp_marker[1024],temp_trace[1024];
    snprintf(temp_marker,sizeof temp_marker,"%s/temporary-fault",folder);
    snprintf(temp_trace,sizeof temp_trace,"%s/temporary-trace",folder);
    CHECK(!setenv("TM_PROJECT_TEMP_MARKER",temp_marker,1) && !setenv("TM_PROJECT_TEMP_TRACE",temp_trace,1));
    CHECK(!setenv("TM_PROJECT_TEMP_LINK",decoy,1));
    char *args[]={"studio-project-test","--skip",NULL};
    Studio *studio=studio_create(2,args,48000,TIC80_PIXEL_COLOR_RGBA8888,folder,1,tic_layout_qwerty); CHECK(studio);
    tm_studio_bind(studio); studio_config_get(studio)->data.checkNewVersion=false;
    tm_studio_clock(0); tm_studio_tick(studio,(tic80_input){0});
    CHECK(tm_studio_cart_file_apply(studio,alias));
    CHECK(!strcmp(studioCart(studio)->name,"cart space.tic") && !strcmp(studioCart(studio)->path,path));
    strcpy(getMemory(studio)->cart.code.data,edited);
    tic_fs_changedir(studio_fs(studio),"other"); CHECK(studioCartChanged(studio));
    // Partial write, sync and rename failures preserve the complete source and
    // modified editor state. A successful retry keeps its path and aliases.
    for(fault=1;fault<=3;++fault) {
        short_write=0; CHECK(studioSaveCart(studio,NULL)==CART_SAVE_ERROR);
        CHECK(studioCartChanged(studio)); check_cart(path,original,0);
        CHECK(!strcmp(studioCart(studio)->path,path));
    }
    fault=0; CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK && !studioCartChanged(studio));
    check_cart(path,edited,0); struct stat info; CHECK(!lstat(alias,&info) && S_ISLNK(info.st_mode));
    char wrong[1024]; snprintf(wrong,sizeof wrong,"%s/other/cart space.tic",folder); CHECK(access(wrong,F_OK));
    CHECK(studioSaveCart(studio,"art.png")==CART_SAVE_OK); check_cart(png,edited,1);
    CHECK(tm_studio_cart_file_apply(studio,png)); tic_fs_homedir(studio_fs(studio));
    strcpy(getMemory(studio)->cart.code.data,original);
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK); check_cart(png,original,1);
    // Preserve the source's suffix case and format, including short names and
    // filenames containing an earlier extension-like substring.
    const char *names[]={"UPPER.TIC", "ART.PNG", ".tic", "a.tic.b.TIC", "x"};
    for(unsigned i=0;i<sizeof names/sizeof *names;++i) {
        CHECK(studioSaveCart(studio,names[i])==CART_SAVE_OK);
        char suffix_path[1024],extra[1024];
        snprintf(suffix_path,sizeof suffix_path,"%s/%s%s",folder,names[i],i==4?".tic":"");
        check_cart(suffix_path,original,i==1);
        CHECK(tm_studio_cart_file_apply(studio,suffix_path));
        CHECK(!strcmp(studioCart(studio)->path,suffix_path));
        strcpy(getMemory(studio)->cart.code.data,edited);
        CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK);
        check_cart(suffix_path,edited,i==1);
        snprintf(extra,sizeof extra,"%s.tic",suffix_path); CHECK(access(extra,F_OK));
        strcpy(getMemory(studio)->cart.code.data,original);
    }
    CartName name=*studioCart(studio);
    CHECK(!tm_studio_cart_file_apply(studio,bad) && !tm_studio_cart_file_apply(studio,fifo));
    CHECK(!memcmp(&name,studioCart(studio),sizeof name));
    char oversized[TICNAME_MAX]; memset(oversized,'x',sizeof oversized); oversized[sizeof oversized-1]=0;
    char destination[TICNAME_MAX]; CHECK(!tm_studio_cart_destination(studio,oversized,false,destination));
    CHECK(studioSaveCart(studio,oversized)==CART_SAVE_ERROR);
    // Upstream browser/project loads must record the resolved source too;
    // a virtual root path is not an absolute POSIX destination for Save.
    CHECK(studioSaveCart(studio,"/nested/project.lua")==CART_SAVE_OK);
    CHECK(studioLoadCart(studio,"/nested/project.lua"));
    char project[1024]; snprintf(project,sizeof project,"%s/nested/project.lua",folder);
    CHECK(!strcmp(studioCart(studio)->name,"project.lua") && !strcmp(studioCart(studio)->path,project));
    tic_fs_changedir(studio_fs(studio),"other");
    strcpy(getMemory(studio)->cart.code.data,edited);
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK);
    tic_fs_homedir(studio_fs(studio)); CHECK(studioLoadCart(studio,"nested/project.lua"));
    CHECK(!strcmp(getMemory(studio)->cart.code.data,edited));
    name=*studioCart(studio); CHECK(!studioLoadCart(studio,bad));
    CHECK(!memcmp(&name,studioCart(studio),sizeof name));
    // First default Save of an unbound cart must not replace a file created
    // after name selection but before publication. The succeeding copy is
    // bound to its own path and later Save updates that copy.
    studioSetCartName(studio,"race.tic","");
    strcpy(getMemory(studio)->cart.code.data,edited);
    publication_race=1;
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK && !studioCartChanged(studio));
    char raced[1024],copy[1024];
    snprintf(raced,sizeof raced,"%s/race.tic",folder); snprintf(copy,sizeof copy,"%s/race-2.tic",folder);
    check_cart(raced,"function TIC() cls(3) end\n",0); check_cart(copy,edited,0);
    CHECK(!strcmp(studioCart(studio)->name,"race-2.tic") && !strcmp(studioCart(studio)->path,copy));
    strcpy(getMemory(studio)->cart.code.data,original);
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK); check_cart(copy,original,0);
    check_cart(raced,"function TIC() cls(3) end\n",0);
    studioSetCartName(studio,"unsupported.tic",""); strcpy(getMemory(studio)->cart.code.data,edited);
    publication_failure=1;
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_ERROR && studioCartChanged(studio));
    CHECK(!*studioCart(studio)->path); publication_failure=0;
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK && !studioCartChanged(studio));
    // Publication succeeded but directory sync failed. Retain the new path
    // while reporting failure and keeping modifications, so retry updates
    // this copy rather than selecting another filename.
    studioSetCartName(studio,"sync.tic",""); strcpy(getMemory(studio)->cart.code.data,original);
    directory_sync_failures=1;
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_ERROR && studioCartChanged(studio));
    char synced[1024],duplicate[1024],redirect[1024];
    snprintf(synced,sizeof synced,"%s/sync.tic",folder);
    snprintf(duplicate,sizeof duplicate,"%s/sync-2.tic",folder);
    check_cart(synced,original,0);
    CHECK(!strcmp(studioCart(studio)->path,synced));
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK && !studioCartChanged(studio));
    CHECK(access(duplicate,F_OK)); check_cart(synced,original,0);
    // The same rule applies to an explicitly named destination: retry must
    // not write the old source after a successful rename to a new file.
    strcpy(getMemory(studio)->cart.code.data,edited); directory_sync_failures=1;
    CHECK(studioSaveCart(studio,"redirect.tic")==CART_SAVE_ERROR && studioCartChanged(studio));
    snprintf(redirect,sizeof redirect,"%s/redirect.tic",folder);
    CHECK(!strcmp(studioCart(studio)->path,redirect));
    check_cart(redirect,edited,0); check_cart(synced,original,0);
    CHECK(studioSaveCart(studio,NULL)==CART_SAVE_OK && !studioCartChanged(studio));
    check_cart(synced,original,0);
    studio_delete(studio);
    tm_studio_session *s=NULL;
    CHECK(tm_studio_session_open(&s,oversized)==TM_STUDIO_ERROR && !s);
    // CLI source files can live outside the editor's browser root.
    CHECK(tm_studio_session_open(&s,other)==TM_STUDIO_OK);
    CHECK(tm_studio_session_load_file(s,alias,1000)==TM_STUDIO_OK); idle(s,120);
    CHECK(!strcmp(tm_studio_session_name(s)->path,path));
    CHECK(tm_studio_session_load_file(s,bad,1000)==TM_STUDIO_CART_ERROR);
    CHECK(tm_studio_session_load_file(s,fifo,1000)==TM_STUDIO_CART_ERROR);
    CHECK(!strcmp(tm_studio_session_name(s)->path,path));
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK);
    CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"-- retained source edit\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
    // Timeout inside a file write kills unacknowledged work; the old source
    // survives and the restored editor can retry Save using the same path.
    unsigned temporary_before=temporary_count(nested);
    file=fopen(marker,"wb"); CHECK(file && !fclose(file));
    CHECK(key(s,tic_key_s,tic_key_ctrl,50)==TM_STUDIO_RECOVERED);
    CHECK(tm_studio_session_modified(s) && !strcmp(tm_studio_session_name(s)->path,path));
    CHECK(temporary_count(nested)==temporary_before); check_cart(decoy,original,0);
    check_cart(path,edited,0); CHECK(!unlink(marker)); idle(s,2);
    CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_OK && !tm_studio_session_modified(s));
    char saved[TIC_CODE_SIZE]; strcpy(saved,tm_studio_session_cart(s)->code.data); check_cart(path,saved,0);
    pid_t previous=tm_studio_session_pid(s); CHECK(!kill(previous,SIGKILL));
    CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(!strcmp(tm_studio_session_name(s)->path,path) && !tm_studio_session_modified(s));
    // The supervisor accepts the destination from a completed failed Save
    // ACK, but still retains the unsaved hash through worker replacement.
    u8 *incoming=malloc(2*sizeof(tic_cartridge)); CHECK(incoming);
    s32 incoming_size=tic_cart_save(tm_studio_session_cart(s),incoming);
    CHECK(tm_studio_session_begin_select_source(s,incoming,incoming_size,"worker-copy.tic","",1000)==TM_STUDIO_OK);
    int selection; do { selection=tm_studio_session_poll(s,10); } while(selection==TM_STUDIO_PENDING);
    CHECK(selection==TM_STUDIO_CART_SELECTED && !*tm_studio_session_name(s)->path); free(incoming);
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"-- retry after directory sync error\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
    snprintf(marker,sizeof marker,"%s/directory-sync-failure",folder);
    CHECK(!setenv("TM_PROJECT_DIRSYNC_MARKER",marker,1));
    // A worker already running cannot inherit new environment, so restart
    // from its accepted editable checkpoint before creating the fault marker.
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL));
    CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    file=fopen(marker,"wb"); CHECK(file && !fclose(file));
    CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
    snprintf(copy,sizeof copy,"%s/worker-copy.tic",other);
    snprintf(duplicate,sizeof duplicate,"%s/worker-copy-2.tic",other);
    CHECK(!strcmp(tm_studio_session_name(s)->path,copy));
    check_cart(copy,tm_studio_session_cart(s)->code.data,0);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL));
    CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED && tm_studio_session_modified(s));
    CHECK(!strcmp(tm_studio_session_name(s)->path,copy));
    CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_OK && !tm_studio_session_modified(s));
    CHECK(access(duplicate,F_OK)); check_cart(copy,tm_studio_session_cart(s)->code.data,0);
    incoming=malloc(2*sizeof(tic_cartridge)); CHECK(incoming);
    incoming_size=tic_cart_save(tm_studio_session_cart(s),incoming);
    CHECK(tm_studio_session_begin_select_source(s,incoming,incoming_size,"unack-copy.tic","",1000)==TM_STUDIO_OK);
    do { selection=tm_studio_session_poll(s,10); } while(selection==TM_STUDIO_PENDING);
    CHECK(selection==TM_STUDIO_CART_SELECTED && !*tm_studio_session_name(s)->path); free(incoming);
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"-- published before worker loss\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
    strcpy(saved,tm_studio_session_cart(s)->code.data);
    file=fopen(publish_marker,"wb"); CHECK(file && !fclose(file));
    CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_RECOVERED);
    snprintf(copy,sizeof copy,"%s/unack-copy.tic",other);
    snprintf(duplicate,sizeof duplicate,"%s/unack-copy-2.tic",other);
    check_cart(copy,saved,0);
    CHECK(tm_studio_session_modified(s) && !strcmp(tm_studio_session_cart(s)->code.data,saved));
    CHECK(!strcmp(tm_studio_session_name(s)->path,copy));
    CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_OK && !tm_studio_session_modified(s));
    CHECK(access(duplicate,F_OK)); check_cart(copy,saved,0);
    const char actions[]={'B','H','E','R','F'};
    for(unsigned test=0;test<sizeof actions;++test) {
        char candidate[32]; snprintf(candidate,sizeof candidate,"journal-%c.tic",actions[test]);
        incoming=malloc(2*sizeof(tic_cartridge)); CHECK(incoming);
        incoming_size=tic_cart_save(tm_studio_session_cart(s),incoming);
        CHECK(tm_studio_session_begin_select_source(s,incoming,incoming_size,candidate,"",1000)==TM_STUDIO_OK);
        do { selection=tm_studio_session_poll(s,10); } while(selection==TM_STUDIO_PENDING);
        CHECK(selection==TM_STUDIO_CART_SELECTED && !*tm_studio_session_name(s)->path); free(incoming);
        CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_clipboard(s,"-- Save publication fault\n")==TM_STUDIO_OK);
        CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
        strcpy(saved,tm_studio_session_cart(s)->code.data);
        temporary_before=temporary_count(other);
        file=fopen(publish_marker,"wb"); CHECK(file && fputc(actions[test],file)!=EOF && !fclose(file));
        if(actions[test]=='H') {
            tic80_input input={0}; input.keyboard.keys[0]=tic_key_s; input.keyboard.keys[1]=tic_key_ctrl;
            CHECK(tm_studio_session_begin_tick(s,input,frames++*1000000000ULL/60,50)==TM_STUDIO_OK);
            int response;
            do {
                response=tm_studio_session_poll(s,0);
                if(response==TM_STUDIO_PENDING) {
                    CHECK(!*tm_studio_session_name(s)->path && tm_studio_session_modified(s));
                    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,saved));
                    struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
                }
            } while(response==TM_STUDIO_PENDING);
            CHECK(response==TM_STUDIO_RECOVERED); idle(s,1);
        } else CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_RECOVERED);
        CHECK(tm_studio_session_modified(s) && !strcmp(tm_studio_session_cart(s)->code.data,saved));
        CHECK(temporary_count(other)==temporary_before);
        snprintf(copy,sizeof copy,"%s/%s",other,candidate);
        snprintf(duplicate,sizeof duplicate,"%s/journal-%c-2.tic",other,actions[test]);
        if(actions[test]=='H') { CHECK(!strcmp(tm_studio_session_name(s)->path,copy)); check_cart(copy,saved,0); }
        else CHECK(!*tm_studio_session_name(s)->path);
        if(actions[test]=='B') CHECK(access(copy,F_OK));
        char external[TIC_CODE_SIZE]; strcpy(external,saved);
        char *color=strstr(external,"cls("); CHECK(color); color[4]='3';
        if(actions[test]=='E') check_cart(copy,external,0);
        if(actions[test]=='R') check_cart(copy,"function TIC() cls(3) end\n",0);
        if(actions[test]=='F') { CHECK(!lstat(copy,&info) && S_ISFIFO(info.st_mode)); }
        CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_OK && !tm_studio_session_modified(s));
        if(actions[test]=='E' || actions[test]=='R' || actions[test]=='F') {
            CHECK(!strcmp(tm_studio_session_name(s)->path,duplicate)); check_cart(duplicate,saved,0);
            if(actions[test]=='E') check_cart(copy,external,0);
            else if(actions[test]=='R') check_cart(copy,"function TIC() cls(3) end\n",0);
            else CHECK(!lstat(copy,&info) && S_ISFIFO(info.st_mode));
        } else { CHECK(access(duplicate,F_OK)); check_cart(copy,saved,0); }
        printf("Save publication recovery %c passed\n",actions[test]);
    }
    const char temp_actions[]={'K','P','R','L','F'};
    for(unsigned test=0;test<sizeof temp_actions;++test) {
        CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
        CHECK(tm_studio_session_clipboard(s,"-- interrupted private temporary\n")==TM_STUDIO_OK);
        CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
        strcpy(saved,tm_studio_session_cart(s)->code.data);
        temporary_before=temporary_count(other);
        file=fopen(temp_marker,"wb"); CHECK(file && fputc(temp_actions[test],file)!=EOF && !fclose(file));
        CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_RECOVERED);
        CHECK(tm_studio_session_modified(s) && !strcmp(tm_studio_session_cart(s)->code.data,saved));
        char interrupted[4096]; file=fopen(temp_trace,"rb"); CHECK(file && fgets(interrupted,sizeof interrupted,file) && !fclose(file));
        if(temp_actions[test]=='K' || temp_actions[test]=='P') {
            CHECK(access(interrupted,F_OK) && temporary_count(other)==temporary_before);
        } else {
            CHECK(temporary_count(other)==temporary_before+1);
            if(temp_actions[test]=='R') check_cart(interrupted,"function TIC() cls(3) end\n",0);
            if(temp_actions[test]=='L') { CHECK(!lstat(interrupted,&info) && S_ISLNK(info.st_mode)); check_cart(decoy,original,0); }
            if(temp_actions[test]=='F') CHECK(!lstat(interrupted,&info) && S_ISFIFO(info.st_mode));
        }
        CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_OK && !tm_studio_session_modified(s));
        if(temp_actions[test]=='R') check_cart(interrupted,"function TIC() cls(3) end\n",0);
        if(temp_actions[test]=='L') CHECK(!lstat(interrupted,&info) && S_ISLNK(info.st_mode));
        if(temp_actions[test]=='F') CHECK(!lstat(interrupted,&info) && S_ISFIFO(info.st_mode));
        check_cart(decoy,original,0);
        printf("Save temporary recovery %c passed\n",temp_actions[test]);
    }
    // An explicit named PNG Save publishes a new destination while the last
    // accepted source remains unchanged. Recover the new name before retry.
    char old_source[TICNAME_MAX],old_code[TIC_CODE_SIZE];
    strcpy(old_source,tm_studio_session_name(s)->path); strcpy(old_code,tm_studio_session_cart(s)->code.data);
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"-- named PNG publication\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
    strcpy(saved,tm_studio_session_cart(s)->code.data);
    CHECK(key(s,tic_key_escape,0,1000)==TM_STUDIO_OK && tm_studio_session_mode(s)==TIC_CONSOLE_MODE); idle(s,120);
    CHECK(tm_studio_session_clipboard(s,"save named-unack.PNG")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK);
    file=fopen(publish_marker,"wb"); CHECK(file && !fclose(file));
    CHECK(key(s,tic_key_return,0,1000)==TM_STUDIO_RECOVERED);
    snprintf(copy,sizeof copy,"%s/named-unack.PNG",other);
    CHECK(!strcmp(tm_studio_session_name(s)->path,copy) && tm_studio_session_modified(s));
    CHECK(!strcmp(tm_studio_session_cart(s)->code.data,saved));
    check_cart(copy,saved,1); check_cart(old_source,old_code,0);
    CHECK(key(s,tic_key_s,tic_key_ctrl,1000)==TM_STUDIO_OK && !tm_studio_session_modified(s));
    check_cart(copy,saved,1); check_cart(old_source,old_code,0);
    puts("Save publication recovery named PNG passed");
    incoming=malloc(2*sizeof(tic_cartridge)); CHECK(incoming);
    incoming_size=tic_cart_save(tm_studio_session_cart(s),incoming);
    CHECK(tm_studio_session_begin_select_source(s,incoming,incoming_size,"fresh-after-save.tic","",1000)==TM_STUDIO_OK);
    do { selection=tm_studio_session_poll(s,10); } while(selection==TM_STUDIO_PENDING);
    CHECK(selection==TM_STUDIO_CART_SELECTED && !*tm_studio_session_name(s)->path); free(incoming);
    CHECK(!kill(tm_studio_session_pid(s),SIGKILL));
    CHECK(tick(s,(tic80_input){0},1000)==TM_STUDIO_RECOVERED);
    CHECK(!*tm_studio_session_name(s)->path && !strcmp(tm_studio_session_name(s)->name,"fresh-after-save.tic"));
    puts("Save publication recovery stale intent rejected");
    CHECK(key(s,tic_key_f1,0,1000)==TM_STUDIO_OK); CHECK(key(s,tic_key_end,tic_key_ctrl,1000)==TM_STUDIO_OK);
    CHECK(tm_studio_session_clipboard(s,"-- cancel Save at core departure\n")==TM_STUDIO_OK);
    CHECK(key(s,tic_key_v,tic_key_ctrl,1000)==TM_STUDIO_OK && tm_studio_session_modified(s));
    CHECK(!unlink(temp_trace)); temporary_before=temporary_count(other);
    snprintf(marker,sizeof marker,"%s/stall",folder);
    file=fopen(marker,"wb"); CHECK(file && !fclose(file));
    tic80_input save_input={0}; save_input.keyboard.keys[0]=tic_key_s; save_input.keyboard.keys[1]=tic_key_ctrl;
    CHECK(tm_studio_session_begin_tick(s,save_input,frames++*1000000000ULL/60,5000)==TM_STUDIO_OK);
    unsigned waits=0;
    while(access(temp_trace,F_OK)) { CHECK(++waits<2000); struct timespec delay={0,1000000}; nanosleep(&delay,NULL); }
    CHECK(temporary_count(other)==temporary_before+1);
    CHECK(tm_studio_session_close(s)==TM_STUDIO_OK);
    CHECK(temporary_count(other)==temporary_before);
    snprintf(copy,sizeof copy,"%s/fresh-after-save.tic",other); CHECK(access(copy,F_OK));
    check_cart(decoy,original,0);
    puts("Save temporary close cancellation passed");
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    CHECK(!nftw(folder,remove_entry,16,FTW_DEPTH|FTW_PHYS));
    puts("Studio project paths: nested/external source, cwd-independent Save, alias retention, PNG/project roundtrip, uppercase formats and short suffix bounds, invalid/FIFO rejection, path bounds, partial-write/fsync/rename failures, interrupted Save and worker recovery passed; post-publication sync failures retain the destination and modified checkpoint across retry and worker replacement; unacknowledged publication recovers the verified destination while preserving edits, and rejects missing/changed/replaced/FIFO files; recorded private temporaries are reclaimed after interruption while decoys, replacements, symlinks and FIFOs survive");
    return 0;
}
