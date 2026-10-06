#define main tm_reader_probe_main
#include "hid_pan_probe.c"
#undef main
/* Development-only extension of the reader fixture. Here reader remains NULL:
 * only the independently launched, unchanged production frontend reads pan.
 * The cartridge's real mouse() calls publish CRC-protected persistent values.
 * A virtual keyboard opens/closes Main's OSD; the FPGA epoch is read, never set.
 */
#include "tic80_mister/memory_map.h"
#include <sys/mman.h>
static mouse keyboard;
static volatile uint32_t *region;
static uint32_t cart_ticket, cart_size, last_ticks;
static pid_t frontend_pid;
static const char *save_path, *frontend_pid_path;
static uint32_t observed_request,observed_ack,observed_status;
static unsigned observations;

static int owns(void)
{
    char path[80];struct stat metadata;
    snprintf(path,sizeof path,"/proc/%ld",(long)main_pid);
    if(stat(path,&metadata)) return 0;
    if(frontend_pid>0 && kill(frontend_pid,0)) return 0;
    return selected() && region && region[TM_IDENTITY_OFFSET/4]==TM_MAGIC &&
        region[TM_GEOMETRY_OFFSET/4+1]==TM_LINUX_INPUT_MAGIC &&
        region[TM_CART_META_OFFSET/4]==cart_ticket &&
        region[TM_CART_META_OFFSET/4+1]==cart_size;
}
static int api_pump(unsigned duration)
{
    uint64_t end=now()+duration;uint32_t ignored=0;
    do {
        if(!owns() || pump(5,0,0,&ignored)) return -1;
        if(keyboard.uhid>=0 && service(&keyboard)) return -1;
        uint32_t request=region[TM_SESSION_REQUEST_OFFSET/4],ack=region[TM_SESSION_ACK_OFFSET/4],status=region[TM_STATUS_OFFSET/4];
        if(observations<64 && (request!=observed_request || ack!=observed_ack || status!=observed_status)) {
            fprintf(stderr,"Transport observation: ms=%llu request=%u ack=%u status=%08x written=%u played=%u publish=%u presented=%u mouse=%08x\n",
                    (unsigned long long)now(),request,ack,status,region[TM_AUDIO_WRITE_OFFSET/4],region[TM_AUDIO_READ_OFFSET/4],
                    region[TM_VIDEO_PUBLISH_OFFSET/4],region[TM_VIDEO_PRESENTED_OFFSET/4],region[TM_MOUSE_OFFSET/4]);
            observed_request=request;observed_ack=ack;observed_status=status;++observations;
        }
    }while(now()<end);
    return 0;
}
static uint32_t word(const uint8_t *p)
{
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t crc(const uint8_t *p,size_t length)
{
    uint32_t value=0xffffffff;
    for(size_t n=0;n<length;++n) {
        value^=p[n];
        for(unsigned bit=0;bit<8;++bit) value=(value>>1)^(0xedb88320u & (0u-(value&1)));
    }
    return ~value;
}
static int decode_persistent(const uint8_t *bytes,size_t size,uint32_t values[16])
{
    if(size!=1036 || memcmp(bytes,"TMPM",4) || word(bytes+4)!=1 || crc(bytes+12,1024)!=word(bytes+8)) return -1;
    for(unsigned n=0;n<16;++n) values[n]=word(bytes+12+4*n);
    if(values[0]!=0x48696450 || values[1]!=1 || values[10]) return -1;
    return 1;
}
static int persistent(uint32_t values[16])
{
    uint8_t bytes[1036];FILE *file=fopen(save_path,"rb");
    if(!file) return errno==ENOENT?0:-1;
    int valid=fread(bytes,1,sizeof bytes,file)==sizeof bytes && fgetc(file)==EOF && !ferror(file);
    fclose(file);
    return valid?decode_persistent(bytes,sizeof bytes,values):-1;
}
static int expect_api(int32_t pan,const char *stage)
{
    uint64_t deadline=now()+10000;uint32_t values[16]={0};
    do {
        if(api_pump(10)) return -1;
        int status=persistent(values);if(status<0) return -1;
        if(status && values[2]>=last_ticks+60 && (int32_t)values[3]==pan) {
            last_ticks=values[2];
            printf("{\"stage\":\"%s\",\"ticks\":%u,\"pan\":%d,\"boot\":%u,\"repeat_mismatches\":%u}\n",
                   stage,values[2],(int32_t)values[3],values[1],values[10]);fflush(stdout);return 0;
        }
    }while(now()<deadline);
    fprintf(stderr,"API stage %s: pan=%d ticks=%u expected=%d after=%u\n",stage,(int32_t)values[3],values[2],pan,last_ticks);
    return -1;
}
static int create_keyboard(void)
{
    static const uint8_t desc[]={5,1,9,6,0xa1,1,5,7,0x19,0xe0,0x29,0xe7,
        0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,1,
        0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,0xc0};
    keyboard.uhid=open("/dev/uhid",O_RDWR|O_NONBLOCK|O_CLOEXEC);
    if(keyboard.uhid<0) return -1;
    struct uhid_event event={0};event.type=UHID_CREATE2;
    snprintf(keyboard.name,sizeof keyboard.name,"TIC-80 native OSD keyboard %ld",(long)getpid());
    snprintf((char *)event.u.create2.name,sizeof event.u.create2.name,"%s",keyboard.name);
    event.u.create2.bus=BUS_USB;event.u.create2.vendor=0xffff;event.u.create2.product=0x0084;
    event.u.create2.rd_size=sizeof desc;memcpy(event.u.create2.rd_data,desc,sizeof desc);
    return write_event(&keyboard,&event);
}
static int menu_key(void)
{
    if(!owns()) return -1;
    struct uhid_event event={0};event.type=UHID_INPUT2;event.u.input2.size=8;
    /* Send the modifier in its own report: Main must process its make before
     * F12, independent of Linux's ordering within a combined HID report. */
    event.u.input2.data[0]=8;
    if(write_event(&keyboard,&event) || api_pump(150)) return -1;
    event.u.input2.data[2]=0x45; /* Left GUI + F12 */
    if(write_event(&keyboard,&event) || api_pump(250)) return -1;
    uint8_t keys[96]={0};
    if(ioctl(keyboard.evdev,EVIOCGKEY(sizeof keys),keys)<0 || !(keys[KEY_LEFTMETA/8]&(1u<<(KEY_LEFTMETA%8))) ||
       !(keys[KEY_F12/8]&(1u<<(KEY_F12%8)))) {
        fprintf(stderr,"Kernel did not receive GUI/F12 keyboard state\n");return -1;
    }
    fprintf(stderr,"OSD key: Linux GUI/F12 held; FPGA keys[0]=%08x keys[8]=%08x mouse=%08x\n",
            region[TM_KEYBOARD_BITS_OFFSET/4],region[TM_KEYBOARD_BITS_OFFSET/4+8],region[TM_MOUSE_OFFSET/4]);
    event.u.input2.data[2]=0;
    if(write_event(&keyboard,&event) || api_pump(50)) return -1;
    memset(event.u.input2.data,0,8);
    return write_event(&keyboard,&event);
}
static uint32_t mouse_gate(void) {return region[TM_MOUSE_OFFSET/4];}
static int wait_osd(int open,uint32_t previous_epoch)
{
    uint64_t deadline=now()+3000;
    do {
        if(api_pump(10)) return -1;
        uint32_t gate=mouse_gate();
        if((int)(gate>>31)==open && ((gate>>19)&4095)==((previous_epoch+1)&4095)) {
            printf("{\"osd_open\":%s,\"epoch\":%u}\n",open?"true":"false",(gate>>19)&4095);fflush(stdout);return 0;
        }
    }while(now()<deadline);
    fprintf(stderr,"Real OSD did not reach open=%d from epoch=%u gate=%08x\n",open,previous_epoch,mouse_gate());return -1;
}
static int api_native(const char *ready)
{
    int result=1,memory=-1;
    keyboard.uhid=keyboard.evdev=-1;
    for(unsigned n=0;n<4;++n) {devices[n].uhid=devices[n].evdev=-1;devices[n].fine=n!=0;devices[n].numbered=n==1||n==3;}
    if(!selected()) return 2;
    char main_path[80],main_name[32]={0};
    snprintf(main_path,sizeof main_path,"/proc/%ld/comm",(long)main_pid);
    FILE *main_file=fopen(main_path,"r");if(!main_file) return 2;
    int valid_main=fgets(main_name,sizeof main_name,main_file)!=NULL;fclose(main_file);
    main_name[strcspn(main_name,"\r\n")]=0;
    if(!valid_main || strcmp(main_name,"MiSTer")) return 2;
    memory=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(memory<0) return 2;
    void *mapped=mmap(NULL,TM_REGION_BYTES,PROT_READ,MAP_SHARED,memory,TM_PHYSICAL_BASE);
    if(mapped==MAP_FAILED) {close(memory);return 2;}region=mapped;
    cart_ticket=region[TM_CART_META_OFFSET/4];cart_size=region[TM_CART_META_OFFSET/4+1];
    if(!owns() || (cart_ticket&3)!=2 || !cart_size || (mouse_gate()>>31)) goto cleanup;
    snprintf(directory,sizeof directory,"/tmp/tic80-hid-api-%ld-XXXXXX",(long)getpid());
    if(!mkdtemp(directory)) goto cleanup;
    for(unsigned n=0;n<4;++n) CHECK(create(&devices[n],n));
    CHECK(create_keyboard());
    uint64_t deadline=now()+15000;
    for(unsigned n=0;n<5;++n) {
        mouse *m=n<4?&devices[n]:&keyboard;
        do {
            CHECK(api_pump(20));
            if(m->evdev>=0) {close(m->evdev);m->evdev=-1;}
            if(*m->alias) {unlink(m->alias);m->alias[0]=0;}
            m->raw_path[0]=0;CHECK(discover(m,n));
            if(now()>deadline) {fprintf(stderr,"API virtual input discovery timeout\n");goto cleanup;}
        }while(!*m->raw_path || m->evdev<0 || !main_holds(m));
        printf("{\"main_pid\":%ld,\"event\":\"%s\",\"raw\":\"%s\",\"main_fd_verified\":true,\"evdev_grab_busy\":true,\"only_main_and_probe_evdev_fds\":true}\n",
               (long)main_pid,m->event_path,m->raw_path);fflush(stdout);
    }
    CHECK(api_pump(100));
    int earlier_sets[5],earlier_gets[4];
    for(unsigned n=0;n<4;++n) {earlier_sets[n]=devices[n].sets;earlier_gets[n]=devices[n].gets;}
    earlier_sets[4]=keyboard.sets;devices[3].blocked=1;
    FILE *file=fopen(ready,"wx");if(!file) goto cleanup;
    fprintf(file,"%ld\n",(long)getpid());if(fclose(file)) goto cleanup;
    deadline=now()+30000;
    do {
        CHECK(api_pump(20));
        uint32_t values[16];int status=persistent(values);CHECK(status<0);
        if(status && values[2]>=120 && devices[1].gets>earlier_gets[1] && devices[2].gets>earlier_gets[2]) {
            FILE *pid_file=fopen(frontend_pid_path,"r");if(!pid_file) goto cleanup;
            long pid=0;int valid=fscanf(pid_file,"%ld",&pid)==1 && pid>1 && pid<=INT32_MAX;
            fclose(pid_file);if(!valid) goto cleanup;frontend_pid=(pid_t)pid;
            CHECK(!owns());
            CHECK(values[3]!=0);last_ticks=values[2];break;
        }
        if(now()>deadline) {fprintf(stderr,"Production frontend and feature reads did not become ready\n");goto cleanup;}
    }while(1);
    CHECK(emit(&devices[0],1));CHECK(expect_api(-1,"coarse-positive"));
    CHECK(emit(&devices[0],-1));CHECK(expect_api(0,"coarse-negative"));
    for(unsigned n=0;n<7;++n) {CHECK(emit(&devices[1],1));CHECK(emit(&devices[2],1));}
    CHECK(expect_api(0,"fine-fractions"));
    CHECK(emit(&devices[1],1));CHECK(emit(&devices[2],1));CHECK(expect_api(-2,"fine-complete"));
    CHECK(api_pump(3500));CHECK(devices[3].gets<=earlier_gets[3]);
    CHECK(emit(&devices[0],1));CHECK(expect_api(-3,"stalled-device-isolation"));
    uint32_t epoch=(mouse_gate()>>19)&4095;CHECK(menu_key());CHECK(wait_osd(1,epoch));
    for(unsigned n=0;n<20;++n) {CHECK(emit(&devices[0],1));CHECK(emit(&devices[1],1));}
    CHECK(expect_api(-3,"menu-motion-suppressed"));
    epoch=(mouse_gate()>>19)&4095;CHECK(menu_key());CHECK(wait_osd(0,epoch));
    CHECK(expect_api(-3,"menu-backlog-suppressed"));
    for(unsigned n=0;n<7;++n) {CHECK(emit(&devices[1],1));CHECK(emit(&devices[2],1));}
    CHECK(expect_api(-3,"fractions-cleared-after-menu"));
    CHECK(emit(&devices[1],1));CHECK(emit(&devices[2],1));CHECK(expect_api(-5,"post-menu-fine"));
    for(unsigned n=0;n<4;++n) {CHECK(!main_holds(&devices[n]));CHECK(devices[n].sets!=earlier_sets[n]);}
    CHECK(!main_holds(&keyboard));CHECK(keyboard.sets!=earlier_sets[4]);
    printf("{\"native_frontend_passed\":true,\"pan\":-5,\"main_pid\":%ld,\"frontend_pid\":%ld,\"blocked_feature_gets\":%d,\"scope\":\"Actual production mouse() API and FPGA OSD epoch; synthetic UHID inputs\"}\n",
           (long)main_pid,(long)frontend_pid,devices[3].gets-earlier_gets[3]);fflush(stdout);result=0;
cleanup:
    for(unsigned n=0;n<5;++n) {
        mouse *m=n<4?&devices[n]:&keyboard;
        if(m->evdev>=0) close(m->evdev);
        if(m->uhid>=0) close(m->uhid);
        if(*m->alias) unlink(m->alias);
    }
    if(*directory) rmdir(directory);
    if(region) munmap((void *)region,TM_REGION_BYTES);
    region=NULL;if(memory>=0) close(memory);return result;
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--self-test")) {
        if(crc((const uint8_t *)"123456789",9)!=0xcbf43926u) return 1;
        uint8_t bytes[1036]={0};uint32_t values[16];
        memcpy(bytes,"TMPM",4);bytes[4]=1;bytes[12]=0x50;bytes[13]=0x64;bytes[14]=0x69;bytes[15]=0x48;bytes[16]=1;
        uint32_t sum=crc(bytes+12,1024);
        for(unsigned n=0;n<4;++n) bytes[8+n]=(uint8_t)(sum>>(8*n));
        if(decode_persistent(bytes,sizeof bytes,values)!=1 || values[0]!=0x48696450) return 1;
        if(decode_persistent(bytes,sizeof bytes-1,values)!=-1) return 1;
        bytes[1024]^=1;if(decode_persistent(bytes,sizeof bytes,values)!=-1) return 1;bytes[1024]^=1;
        bytes[0]='X';if(decode_persistent(bytes,sizeof bytes,values)!=-1) return 1;bytes[0]='T';
        bytes[4]=2;if(decode_persistent(bytes,sizeof bytes,values)!=-1) return 1;bytes[4]=1;
        bytes[16]=2;sum=crc(bytes+12,1024);
        for(unsigned n=0;n<4;++n) bytes[8+n]=(uint8_t)(sum>>(8*n));
        if(decode_persistent(bytes,sizeof bytes,values)!=-1) return 1;
        bytes[16]=1;bytes[52]=1;sum=crc(bytes+12,1024);
        for(unsigned n=0;n<4;++n) bytes[8+n]=(uint8_t)(sum>>(8*n));
        if(decode_persistent(bytes,sizeof bytes,values)!=-1) return 1;
        return self_test();
    }
    if(argc!=6 || strcmp(argv[1],"--frontend") || strncmp(argv[3],"/tmp/",5) ||
        strncmp(argv[4],"/tmp/",5) || strncmp(argv[5],"/tmp/",5)) return 2;
    char *end;errno=0;long pid=strtol(argv[2],&end,10);
    if(errno || *end || pid<=1 || pid>INT32_MAX) return 2;
    main_pid=(pid_t)pid;save_path=argv[3];frontend_pid_path=argv[5];frontend_pid=0;
    signal(SIGINT,stop);signal(SIGTERM,stop);return api_native(argv[4]);
}
