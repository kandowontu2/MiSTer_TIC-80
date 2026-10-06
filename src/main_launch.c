#define _GNU_SOURCE
#include "tic80_mister/main_launch.h"
#include "sxmlc.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct { int inside, found, cart, error; } first_action;
static int action(XMLEvent event,const XMLNode *node,SXML_CHAR *text,int n,SAX_Data *sd)
{
    (void)text; (void)n;
    first_action *a=sd->user;
    if(event==XML_EVENT_ERROR) a->error=1;
    if(event==XML_EVENT_END_NODE && !strcasecmp(node->tag,"mistergamedescription")) a->inside=0;
    if(event!=XML_EVENT_START_NODE || a->found) return 1;
    if(!strcasecmp(node->tag,"mistergamedescription")) { a->inside=1; return 1; }
    if(!a->inside) return 1;
    unsigned valid=0,index=0; char type=0;
    int file=!strcasecmp(node->tag,"file"),reset=!strcasecmp(node->tag,"reset");
    for(int i=0;i<node->n_attributes;++i) {
        const char *name=node->attributes[i].name,*value=node->attributes[i].value;
        if(!strcasecmp(name,"delay")) valid|=1;
        else if(file && !strcasecmp(name,"type")) {
            if(!strcasecmp(value,"f")) { type='F'; valid|=2; }
            else if(!strcasecmp(value,"s")) { type='S'; valid|=2; }
        } else if(file && !strcasecmp(name,"index")) { index=(unsigned)strtoul(value,NULL,0); valid|=4; }
        else if(file && !strcasecmp(name,"path")) valid|=8;
    }
    /* Main ignores incomplete actions and uses strtoul(base 0) for index.
     * A reset or non-cartridge first action must keep its original behavior. */
    if((file && valid==15) || (reset && (valid&1))) {
        a->found=1; a->cart=file && type=='F' && (index==0 || index==0x40);
    }
    return 1;
}
int tm_main_mgl_initial_cart(const char *xml)
{
    if(!xml) return -1;
    first_action a={0}; SAX_Callbacks callbacks;
    SAX_Callbacks_init(&callbacks); callbacks.all_event=action;
    XMLDoc_parse_buffer_SAX(xml,"MiSTer launch",&callbacks,&a);
    /* Main retains actions parsed before an XML error. Match that behavior. */
    return a.found?a.cart:a.error?-1:0;
}
static int read_bounded_at(int directory,const char *name,char *buffer,size_t capacity)
{
    int fd=openat(directory,name,O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if(fd<0) return -1;
    size_t used=0; int result=-1;
    while(used<capacity) {
        ssize_t got=read(fd,buffer+used,capacity-used);
        if(got<0 && errno==EINTR) continue;
        if(got<0) break;
        if(!got) { result=(int)used; break; }
        used+=(size_t)got;
    }
    close(fd); return result;
}
static int matches(int directory)
{
    char exe[PATH_MAX]; ssize_t n=readlinkat(directory,"exe",exe,sizeof exe-1);
    if(n<0 || (size_t)n>=sizeof exe-1) return 0;
    exe[n]=0; return !strcmp(exe,"/media/fat/MiSTer");
}
static int context(int directory)
{
    char argv[8192],again[8192];
    int length=read_bounded_at(directory,"cmdline",argv,sizeof argv);
    if(length<=0 || argv[length-1]) return -1;
    const char *argument[4]={0}; int count=0;
    for(int at=0;at<length && count<4;) { argument[count++]=argv+at; at+=(int)strlen(argv+at)+1; }
    /* app_restart executes Main with RBF argv[1], optional MGL argv[2]. */
    const char *path=count>=3?argument[2]:NULL;
    int result=0;
    if(path && *path) {
        size_t size=strlen(path);
        if(size>=4 && !strcasecmp(path+size-4,".mgl")) {
            /* Bound both file size and parser work; a missing/oversize MGL
             * remains unknown, never permission to BOOT the cached game. */
            char *xml=malloc(65537);
            if(!xml) return -1;
            int bytes=read_bounded_at(AT_FDCWD,path,xml,65537);
            result=-1;
            if(bytes>=0 && bytes<=65536 && !memchr(xml,0,(size_t)bytes)) {
                xml[bytes]=0; result=tm_main_mgl_initial_cart(xml);
            }
            free(xml);
        }
    }
    /* An exec during the observation cannot authorize a stale decision. */
    int second=read_bounded_at(directory,"cmdline",again,sizeof again);
    return matches(directory) && second==length && !memcmp(argv,again,(size_t)length)?result:-1;
}
int tm_main_initial_cart(const char *proc_root)
{
    DIR *proc=opendir(proc_root?proc_root:"/proc");
    if(!proc) return -1;
    struct dirent *entry; int found=0,result=-1;
    while((entry=readdir(proc))) {
        if(!*entry->d_name || strspn(entry->d_name,"0123456789")!=strlen(entry->d_name)) continue;
        int process=openat(dirfd(proc),entry->d_name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(process<0) continue;
        if(matches(process)) { ++found; if(found==1) result=context(process); }
        close(process);
    }
    closedir(proc); return found==1?result:-1;
}
