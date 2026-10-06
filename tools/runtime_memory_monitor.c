#define _GNU_SOURCE
/* Read-only, bounded /proc and save sampler. Run detached on the MiSTer so
 * checkpoint cadence does not depend on an SSH observation connection. */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int signal) { (void)signal; stopping=1; }
static uint64_t now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) { perror("monotonic clock"); exit(1); }
    return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
}
static unsigned number(const char *text, unsigned maximum) {
    char *end; errno=0; unsigned long value=strtoul(text,&end,10);
    return errno || !*text || *end || *text=='-' || !value || value>maximum ? 0 : (unsigned)value;
}
static int selected(const char *path) {
    char name[80]; FILE *file=fopen(path,"r"); if(!file) return 0;
    int valid=fgets(name,sizeof name,file)!=NULL; fclose(file);
    if(!valid) return 0;
    name[strcspn(name,"\r\n")]=0;
    return !strcmp(name,"TIC-80");
}
static int identity(unsigned pid, uint64_t *start, unsigned *parent) {
    char path[80], line[4096]; snprintf(path,sizeof path,"/proc/%u/stat",pid);
    FILE *file=fopen(path,"r"); if(!file) return -1;
    int valid=fgets(line,sizeof line,file)!=NULL; fclose(file);
    char *end=valid?strrchr(line,')'):NULL;
    if(!end || end[1]!=' ' || end[2]=='Z' || end[2]=='X') return -1;
    char *state=NULL, *word=strtok_r(end+2," \n",&state);
    unsigned index=0; *start=0; *parent=0;
    for(; word; word=strtok_r(NULL," \n",&state),++index) {
        if(index==1) *parent=(unsigned)strtoul(word,NULL,10);
        if(index==19) *start=strtoull(word,NULL,10);
    }
    return index>=20 && *start ? 0 : -1;
}
typedef struct {
    uint64_t start;
    unsigned parent, rss, fd[3], settled, peak;
    int nice, scheduler;
    char cpu[8192];
} process;
static int descriptors(unsigned pid, unsigned *count) {
    char path[80]; snprintf(path,sizeof path,"/proc/%u/fd",pid);
    DIR *directory=opendir(path); if(!directory) return -1;
    unsigned n=0; struct dirent *entry;
    while((entry=readdir(directory))) if(entry->d_name[0]!='.') ++n;
    closedir(directory); *count=n; return n>=3?0:-1;
}
static int sample(unsigned pid, process *out) {
    memset(out,0,sizeof *out);
    if(identity(pid,&out->start,&out->parent)) return -1;
    char path[80], line[8192]; snprintf(path,sizeof path,"/proc/%u/status",pid);
    FILE *file=fopen(path,"r"); if(!file) return -1;
    int rss=0,cpu=0;
    while(fgets(line,sizeof line,file)) {
        if(sscanf(line,"VmRSS: %u kB",&out->rss)==1) rss=1;
        if(!strncmp(line,"Cpus_allowed_list:",18)) {
            char *value=line+18; value+=strspn(value," \t");
            value[strcspn(value,"\r\n")]=0;
            if(*value && strspn(value,"0123456789,-")==strlen(value)) {
                snprintf(out->cpu,sizeof out->cpu,"%s",value); cpu=1;
            }
        }
    }
    int failed=ferror(file); fclose(file);
    if(failed || !rss || !cpu) return -1;
    errno=0; out->nice=getpriority(PRIO_PROCESS,(id_t)pid);
    if(errno) return -1;
    out->scheduler=sched_getscheduler((pid_t)pid); if(out->scheduler<0) return -1;
    out->settled=UINT_MAX;
    for(unsigned i=0;i<3;++i) {
        if(descriptors(pid,&out->fd[i])) return -1;
        if(out->fd[i]<out->settled) out->settled=out->fd[i];
        if(out->fd[i]>out->peak) out->peak=out->fd[i];
        if(i<2) { struct timespec wait={0,10000000}; nanosleep(&wait,NULL); }
    }
    uint64_t after; unsigned parent;
    return identity(pid,&after,&parent) || after!=out->start || parent!=out->parent ? -1 : 0;
}
static uint32_t get32(const unsigned char *p) {
    return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int save_values(const char *path, uint32_t values[4]) {
    unsigned char bytes[1037]; FILE *file=fopen(path,"rb"); if(!file) return -1;
    size_t n=fread(bytes,1,sizeof bytes,file); int valid=!ferror(file); fclose(file);
    if(!valid || n!=1036 || memcmp(bytes,"TMPM",4) || get32(bytes+4)!=1) return -1;
    uint32_t crc=UINT32_MAX;
    for(unsigned i=12;i<1036;++i) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    if((crc^UINT32_MAX)!=get32(bytes+8)) return -1;
    for(unsigned i=0;i<4;++i) values[i]=get32(bytes+12+i*4);
    return 0;
}
static void print_process(const char *role,unsigned pid,const process *p) {
    printf(",\"%s\":%u,\"%s_starttime\":%llu,\"%s_cpu\":\"%s\",\"%s_nice\":%d,"
        "\"%s_scheduler\":%d,\"%s_rss_kib\":%u,\"%s_fds\":%u,\"%s_fd_peak\":%u,"
        "\"%s_fd_samples\":[%u,%u,%u]",role,pid,role,(unsigned long long)p->start,
        role,p->cpu,role,p->nice,role,p->scheduler,role,p->rss,role,p->settled,role,p->peak,
        role,p->fd[0],p->fd[1],p->fd[2]);
}
int main(int argc,char **argv) {
    unsigned parent=0,worker=0,seconds=600,interval=30000;
    const char *save=NULL,*core="/tmp/CORENAME";
    for(int i=1;i<argc;++i) {
        if(i+1>=argc) return 2;
        const char *option=argv[i++],*value=argv[i];
        if(!strcmp(option,"--parent")) parent=number(value,INT_MAX);
        else if(!strcmp(option,"--worker")) worker=number(value,INT_MAX);
        else if(!strcmp(option,"--seconds")) seconds=number(value,3600);
        else if(!strcmp(option,"--interval-ms")) interval=number(value,600000);
        else if(!strcmp(option,"--save")) save=value;
        else if(!strcmp(option,"--core-name")) core=value;
        else return 2;
    }
    if(!parent || !worker || parent==worker || !save || !seconds || interval<10) return 2;
    signal(SIGINT,stop); signal(SIGTERM,stop); setvbuf(stdout,NULL,_IONBF,0);
    uint64_t started=now(),deadline=started,identity_parent=0,identity_worker=0;
    uint32_t previous[4]={0}; unsigned checks=0;
    while(!stopping) {
        uint64_t begin=now(); process p,w; uint32_t values[4];
        if(!selected(core) || sample(parent,&p) || sample(worker,&w) || w.parent!=parent ||
            save_values(save,values) || !selected(core)) {
            fputs("Memory checkpoint source disappeared or save/core validation failed\n",stderr); return 1;
        }
        if(checks && (identity_parent!=p.start || identity_worker!=w.start ||
            values[0]<previous[0] || values[1]<previous[1] || values[2]!=previous[2])) {
            fputs("Process identity or completed save regressed\n",stderr); return 1;
        }
        identity_parent=p.start; identity_worker=w.start; memcpy(previous,values,sizeof values);
        if(stopping) return 1;
        uint64_t captured=now();
        printf("{\"elapsed_ns\":%llu,\"captured_monotonic_ns\":%llu,\"capture_duration_ns\":%llu",
            (unsigned long long)(captured-started),(unsigned long long)captured,(unsigned long long)(captured-begin));
        print_process("parent",parent,&p); print_process("worker",worker,&w);
        printf(",\"save_ticks\":%u,\"save_elapsed_ms\":%u,\"save_boots\":%u,\"save_largest_gap_ms\":%u,\"save_CRC_valid\":true}\n",
            values[0],values[1],values[2],values[3]);
        ++checks;
        if(captured-started>=(uint64_t)seconds*1000000000u) return 0;
        deadline+=(uint64_t)interval*1000000u;
        struct timespec wait={(time_t)(deadline/1000000000u),(long)(deadline%1000000000u)};
        int result;
        while((result=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&wait,NULL))==EINTR && !stopping) {}
        if(result && result!=EINTR) return 1;
    }
    return 1;
}
