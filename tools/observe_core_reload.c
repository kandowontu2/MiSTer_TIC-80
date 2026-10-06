#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
/* Read-only reload journal. No transport ownership, locks or DDR writes. */
#include "tic80_mister/memory_map.h"
#include <fcntl.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static uint64_t now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;
}
static uint32_t reg(volatile const uint8_t *memory,unsigned offset) {
    uint32_t value=*(volatile const uint32_t *)(memory+offset);
    __asm__ volatile("dmb sy" ::: "memory");
    return value;
}
/* Restricted to the installed Main executable and four MGL/UI scalar fields.
 * Addresses come from the retained 607120d3 Main ELF; its profile is guarded
 * by the caller before dispatch. This neither attaches nor stops the process. */
static int main_fd=-1,main_pid;
static int main_values(uint32_t *menu,uint32_t head[2],uint32_t tail[3]) {
    for(unsigned attempt=0;attempt<2;++attempt) {
        if(main_fd>=0 && pread(main_fd,menu,4,0x00310894)==4 &&
           pread(main_fd,head,8,0x00b60a10)==8 && pread(main_fd,tail,12,0x00b622a8)==12) return 0;
        if(main_fd>=0) close(main_fd);
        main_fd=-1; main_pid=0;
        DIR *proc=opendir("/proc"); if(!proc) return -1;
        struct stat installed={0}; if(stat("/media/fat/MiSTer",&installed)) { closedir(proc); return -1; }
        struct dirent *entry;
        while((entry=readdir(proc))) {
            char *end; long pid=strtol(entry->d_name,&end,10); if(!*entry->d_name || *end || pid<=0) continue;
            char path[80]; snprintf(path,sizeof path,"/proc/%ld/exe",pid);
            struct stat image={0}; if(stat(path,&image) || image.st_dev!=installed.st_dev || image.st_ino!=installed.st_ino) continue;
            snprintf(path,sizeof path,"/proc/%ld/mem",pid); main_fd=open(path,O_RDONLY);
            if(main_fd>=0) { main_pid=(int)pid; break; }
        }
        closedir(proc);
    }
    return -1;
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    char *end; unsigned long seconds=strtoul(argv[1],&end,10);
    if(!*argv[1] || *end || seconds<1 || seconds>60) return 2;
    FILE *journal=fopen(argv[2],"wx"); if(!journal) return 1;
    int fd=open("/dev/mem",O_RDONLY|O_SYNC); if(fd<0) { fclose(journal); return 1; }
    volatile const uint8_t *memory=mmap(NULL,TM_REGION_BYTES,PROT_READ,MAP_SHARED,fd,TM_PHYSICAL_BASE);
    if(memory==MAP_FAILED) { close(fd); fclose(journal); return 1; }
    /* Pinned Main fpga_base_addr_ac5.h / fpga_io.cpp: manager GPO/GPI.
     * Both are regular read-only observations, not FIFO or read-clear ports. */
    volatile const uint8_t *manager=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,0xff706000ULL);
    if(manager==MAP_FAILED) { munmap((void *)memory,TM_REGION_BYTES); close(fd); fclose(journal); return 1; }
    setvbuf(journal,NULL,_IOLBF,0);
    uint64_t start=now(),until=start+seconds*1000000000ULL;
    do {
        char core[80]={0}; FILE *name=fopen("/tmp/CORENAME","r");
        if(!name) break;
        int read=fgets(core,sizeof core,name)!=NULL; fclose(name);
        core[strcspn(core,"\r\n")]=0;
        if(!read || strcmp(core,"TIC-80")) break;
        struct stat identity={0}; if(stat("/tmp/CORENAME",&identity)) break;
        uint32_t menu=0,head[2]={0},tail[3]={0}; int main_ok=main_values(&menu,head,tail)==0;
        fprintf(journal,"{\"ns\":%llu,\"status\":%u,\"cart\":%u,\"ack\":%u,\"request\":%u,\"session\":%u,\"heartbeat\":%u,\"name_s\":%lld,\"name_ns\":%ld,\"gpo\":%u,\"gpi\":%u,\"main_pid\":%d,\"main_ok\":%d,\"menu\":%u,\"mgl_count\":%u,\"mgl_current\":%u,\"mgl_state\":%u,\"mgl_done\":%u}\n",
            (unsigned long long)(now()-start),reg(memory,TM_STATUS_OFFSET),reg(memory,TM_CART_META_OFFSET),
            reg(memory,TM_CART_ACK_OFFSET),reg(memory,TM_SESSION_REQUEST_OFFSET),reg(memory,TM_SESSION_ACK_OFFSET),
            reg(memory,TM_HEARTBEAT_OFFSET),(long long)identity.st_mtim.tv_sec,identity.st_mtim.tv_nsec,
            reg(manager,0x10),reg(manager,0x14),main_pid,main_ok,menu,head[0],head[1],tail[1],tail[2]);
        struct timespec pause={0,10000000}; nanosleep(&pause,NULL);
    } while(now()<until);
    munmap((void *)manager,4096); munmap((void *)memory,TM_REGION_BYTES); close(fd);
    if(main_fd>=0) close(main_fd);
    return fclose(journal)!=0;
}
