/* Test-only corruption of one worker acknowledgement's ancillary data. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
ssize_t __real_sendmsg(int,const struct msghdr*,int);
ssize_t __wrap_sendmsg(int socket,const struct msghdr *message,int flags)
{
    const char *marker=getenv("TM_TEST_HISTORY_FAULT");
    struct cmsghdr *rights=CMSG_FIRSTHDR(message);
    if(!marker || !rights || rights->cmsg_level!=SOL_SOCKET || rights->cmsg_type!=SCM_RIGHTS)
        return __real_sendmsg(socket,message,flags);
    int input=open(marker,O_RDONLY|O_CLOEXEC); if(input<0) return __real_sendmsg(socket,message,flags);
    char action=0; ssize_t got=read(input,&action,1); close(input);
    if(got!=1 || unlink(marker)) _exit(91);
    struct msghdr fault=*message;
    union { struct cmsghdr align; char bytes[CMSG_SPACE(253*sizeof(int))]; } control={0};
    int original=-1,copy=-1; memcpy(&original,CMSG_DATA(rights),sizeof original);
    int descriptors[253]; unsigned count=1;
    descriptors[0]=original;
    char opcode='X'; struct iovec data={&opcode,1};
    switch(action) {
    case 'D': count=0; break;
    case 'T': count=2; descriptors[1]=original; break;
    case 'C': count=8; for(unsigned i=1;i<count;++i) descriptors[i]=original; break;
    case 'M': count=253; for(unsigned i=1;i<count;++i) descriptors[i]=original; break;
    case 'X': fault.msg_iov=&data; fault.msg_iovlen=1; break;
    case 'U': case 'W': {
        struct stat info; unsigned char header[32];
        if(fstat(original,&info) || pread(original,header,sizeof header,0)!=sizeof header) _exit(92);
        copy=memfd_create("fault-history",MFD_ALLOW_SEALING|MFD_CLOEXEC);
        if(copy<0 || ftruncate(copy,info.st_size)) _exit(93);
        if(action=='W') {
            unsigned magic,request; memcpy(&magic,header,4); unsigned offset=magic==0x32485554u?12:20;
            memcpy(&request,header+offset,4); ++request; memcpy(header+offset,&request,4);
        }
        if(pwrite(copy,header,sizeof header,0)!=sizeof header) _exit(94);
        if(action=='W' && fcntl(copy,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)) _exit(95);
        descriptors[0]=copy; break;
    }
    default: _exit(96);
    }
    fault.msg_control=count?control.bytes:NULL;
    fault.msg_controllen=count?CMSG_SPACE(count*sizeof(int)):0;
    if(count) {
        struct cmsghdr *c=CMSG_FIRSTHDR(&fault); c->cmsg_level=SOL_SOCKET; c->cmsg_type=SCM_RIGHTS;
        c->cmsg_len=CMSG_LEN(count*sizeof(int)); memcpy(CMSG_DATA(c),descriptors,count*sizeof(int));
    }
    ssize_t result=__real_sendmsg(socket,&fault,flags); if(copy>=0) close(copy); return result;
}
/* The production session selects glibc's time64 redirect on 32-bit ARM. */
ssize_t __wrap___sendmsg64(int socket,const struct msghdr *message,int flags)
{ return __wrap_sendmsg(socket,message,flags); }
