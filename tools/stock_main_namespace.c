#define _GNU_SOURCE
/* Hardware-test helper only. Bind stock Main over its configured path inside
 * a new private mount namespace; never write or rename the shared executable. */
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=3 || (strcmp(argv[1],"--probe") && strcmp(argv[1],"--run"))) return 2;
    struct stat before,after,source,target;
    if(stat("/proc/self/ns/mnt",&before) || stat(argv[2],&source) ||
       stat("/media/fat/MiSTer",&target) || !S_ISREG(source.st_mode) || !S_ISREG(target.st_mode)) return 2;
    if(unshare(CLONE_NEWNS)) {perror("unshare");return 1;}
    if(stat("/proc/self/ns/mnt",&after) || before.st_ino==after.st_ino) {
        fprintf(stderr,"No distinct mount namespace; refusing mount\n");return 1;
    }
    /* Fail before the bind if propagation cannot be made namespace-local. */
    if(mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)) {perror("private mounts");return 1;}
    if(mount(argv[2],"/media/fat/MiSTer",NULL,MS_BIND,NULL)) {perror("bind stock Main");return 1;}
    printf("stock-test namespace: original=%lu private=%lu\n",(unsigned long)before.st_ino,(unsigned long)after.st_ino);
    fflush(stdout);
    if(!strcmp(argv[1],"--probe")) {
        execl("/bin/sh","sh","-c","readlink /proc/self/ns/mnt; sha256sum /media/fat/MiSTer",(char*)NULL);
    } else {
        execl("/media/fat/MiSTer","/media/fat/MiSTer","/media/fat/menu.rbf",(char*)NULL);
    }
    perror("exec");return 1;
}
