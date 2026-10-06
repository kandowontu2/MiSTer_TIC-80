#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tic80_mister/backend.h"
#include "tic80_mister/memory_map.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define REG(m, off) (*(volatile uint32_t *)((m) + (off)))
static void delay(void) { struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
static double seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void name_file(int fd, const char *name, size_t length)
{
    assert(lseek(fd,0,SEEK_SET)==0 && ftruncate(fd,0)==0);
    assert(write(fd,name,length)==(ssize_t)length);
}
static int departure_fd=-1;
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *bytes,size_t size,size_t count,FILE *file)
{
    size_t result=__real_fread(bytes,size,count,file);
    // Main changes selection immediately after this reader observed TIC-80.
    // DDR identity/session words intentionally stay valid and stale.
    if(departure_fd>=0 && size*result==6 && !memcmp(bytes,"TIC-80",6)) {
        int fd=departure_fd; departure_fd=-1; name_file(fd,"MENU",4);
    }
    return result;
}
int main(void)
{
    char path[] = "/tmp/tic80-backend-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && ftruncate(fd, TM_REGION_BYTES) == 0);
    volatile uint8_t *m = mmap(NULL, TM_REGION_BYTES, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    assert(m != MAP_FAILED);
    REG(m, TM_IDENTITY_OFFSET) = TM_MAGIC;
    REG(m, TM_GEOMETRY_OFFSET) = (TM_HEIGHT << 16) | TM_WIDTH;
    REG(m, TM_SESSION_REQUEST_OFFSET) = 1; /* stale previous session */
    REG(m, TM_VIDEO_PRESENTED_OFFSET) = 99;
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        uint32_t session = 0, presented = 0, count = 0, audio_read = 0;
        for (;;) {
            REG(m, TM_HEARTBEAT_OFFSET)++;
            uint32_t request = REG(m, TM_SESSION_REQUEST_OFFSET);
            if (request != session) {
                session = request;
                /* Deliberately echo before clearing presented, like separate
                 * FPGA bus writes. The producer must wait for both. */
                REG(m, TM_SESSION_ACK_OFFSET) = session;
                delay();
                REG(m, TM_VIDEO_PRESENTED_OFFSET) = presented = 0;
                REG(m, TM_AUDIO_READ_OFFSET) = audio_read = 0;
            }
            uint32_t audio_write = REG(m, TM_AUDIO_WRITE_OFFSET);
            if (session > 1) while (audio_read != audio_write) {
                unsigned offset = TM_AUDIO_RING_OFFSET + (audio_read & (TM_AUDIO_CAPACITY - 1)) * 4;
                uint32_t expected = (uint16_t)(audio_read + 1) | ((uint32_t)(uint16_t)~(audio_read + 1) << 16);
                if (REG(m, offset) != expected) _exit(5);
                ++audio_read;
                REG(m, TM_AUDIO_READ_OFFSET) = audio_read;
            }
            uint32_t pub = REG(m, TM_VIDEO_PUBLISH_OFFSET);
            if (session > 1 && (pub & 2) && pub != presented) {
                unsigned offset = pub & 1 ? TM_BUFFER1_OFFSET : TM_BUFFER0_OFFSET;
                for (unsigned i = 0; i < TM_FRAME_BYTES; ++i) {
                    if (m[offset + i] != (uint8_t)(count + i)) _exit(3);
                }
                delay();
                /* Recheck the payload while the producer waits for ACK. */
                for (unsigned i = 0; i < TM_FRAME_BYTES; ++i) {
                    if (m[offset + i] != (uint8_t)(count + i)) _exit(4);
                }
                REG(m, TM_VIDEO_PRESENTED_OFFSET) = presented = pub;
                ++count;
            }
            delay();
        }
    }
    tm_backend b;
    assert(tm_backend_open(&b, path) == 0);
    tm_core_generation generation, next_generation;
    assert(tm_backend_core_generation(&b, &generation) == 0);
    assert(tm_backend_start(&b) == 0 && b.session > 1);
    tm_backend second;
    assert(tm_backend_open(&second, path) == 0);
    uint32_t established = REG(m, TM_SESSION_REQUEST_OFFSET);
    assert(tm_backend_start(&second) != 0);
    assert(REG(m, TM_SESSION_REQUEST_OFFSET) == established);
    tm_backend_close(&second);
    uint8_t frame[TM_FRAME_BYTES];
    for (unsigned n = 0; n < 32; ++n) {
        int16_t pcm[1600];
        for (unsigned i = 0; i < 800; ++i) {
            pcm[i*2] = (int16_t)(n * 800 + i + 1);
            pcm[i*2+1] = (int16_t)~(n * 800 + i + 1);
        }
        assert(tm_backend_audio(&b, pcm, 800) == 0);
        for (unsigned i = 0; i < sizeof frame; ++i) frame[i] = (uint8_t)(n + i);
        assert(tm_backend_present(&b, frame, sizeof frame) == 0);
        assert(tm_backend_pace(&b, NULL) == 0);
    }
    assert(tm_backend_drain(&b) == 0);
    uint8_t *cart_copy = NULL;
    size_t cart_size = 0;
    for (unsigned i = 0; i < 4097; ++i) m[TM_CART_DATA_OFFSET+i] = (uint8_t)(i*13);
    REG(m, TM_CART_META_OFFSET+4) = 4097;
    REG(m, TM_CART_META_OFFSET) = 6;
    assert(tm_backend_cart_pending(&b) == 1);
    assert(tm_backend_cart(&b, &cart_copy, &cart_size) == 1 && cart_size == 4097);
    assert(REG(m, TM_CART_ACK_OFFSET) == 6);
    for (unsigned i = 0; i < cart_size; ++i) assert(cart_copy[i] == (uint8_t)(i*13));
    free(cart_copy);
    assert(tm_backend_cart(&b, &cart_copy, &cart_size) == 0);
    REG(m, TM_CART_META_OFFSET+4) = TM_CART_CAPACITY+1;
    REG(m, TM_CART_META_OFFSET) = 10;
    assert(tm_backend_cart(&b, &cart_copy, &cart_size) == 2 && !cart_copy && !cart_size);
    assert(REG(m, TM_CART_ACK_OFFSET) == 10);
    REG(m, TM_CART_META_OFFSET) = 15; // explicit malformed download
    assert(tm_backend_cart(&b, &cart_copy, &cart_size) == 2);
    assert(REG(m, TM_CART_ACK_OFFSET) == 15);
    // Optional capability is explicit; old bridges never authorize stale
    // filename memory. Copy filename and cart before releasing their ticket.
    const char *original_source="/media/fat/games/TIC-80/a.tic";
    char source[256];
    for(unsigned kind=0;kind<7;++kind) {
        uint32_t ticket=18+4*kind;
        REG(m,TM_IDENTITY_OFFSET+4)=kind==1?0:TM_CART_SOURCE_MAGIC;
        REG(m,TM_CART_SOURCE_OFFSET)=kind==2?ticket-4:ticket;
        REG(m,TM_CART_SOURCE_OFFSET+4)=kind==3?257:(uint32_t)strlen(original_source)+1;
        memcpy((void*)(m+TM_CART_SOURCE_OFFSET+8),original_source,strlen(original_source)+1);
        if(kind==4) m[TM_CART_SOURCE_OFFSET+8]='x';
        if(kind==5) m[TM_CART_SOURCE_OFFSET+10]=0;
        if(kind==6) m[TM_CART_SOURCE_OFFSET+8+strlen(original_source)]='x';
        REG(m,TM_CART_META_OFFSET+4)=4; REG(m,TM_CART_META_OFFSET)=ticket;
        assert(tm_backend_cart_source(&b,&cart_copy,&cart_size,source)==1 && cart_size==4);
        assert(REG(m,TM_CART_ACK_OFFSET)==ticket);
        assert(kind? !*source : !strcmp(source,original_source));
        memset((void*)(m+TM_CART_SOURCE_OFFSET+8),'x',strlen(original_source)+1);
        if(!kind) assert(!strcmp(source,original_source));
        free(cart_copy);
    }
    REG(m,TM_IDENTITY_OFFSET+4)=0;
    for (unsigned raw = 0; raw < 256; ++raw) {
        REG(m, TM_JOY0_OFFSET) = raw;
        REG(m, TM_JOY1_OFFSET) = raw ^ 255;
        REG(m, TM_JOY2_OFFSET) = raw;
        REG(m, TM_JOY3_OFFSET) = raw ^ 255;
        uint32_t pads = tm_backend_gamepads(&b);
        unsigned expected = (raw & 0xF0) | ((raw & 8) >> 3) | ((raw & 4) >> 1) |
                            ((raw & 2) << 1) | ((raw & 1) << 3);
        assert(pads == (expected | ((expected ^ 255) << 8) |
                       (expected << 16) | ((expected ^ 255) << 24)));
    }
    tm_input_snapshot inputs;
    REG(m,TM_KEYBOARD_OFFSET)=2;
    for(unsigned i=0;i<16;++i) REG(m,TM_KEYBOARD_BITS_OFFSET+i*4)=0x10203040u+i;
    REG(m,TM_MOUSE_OFFSET)=0x00054178;
    REG(m,TM_MOUSE_OFFSET+4)=0xfffffffe;
    REG(m,TM_HORIZONTAL_WHEEL_OFFSET)=0xdeadbeef;
    for(unsigned i=0;i<4;++i) REG(m,TM_JOY0_OFFSET+i*8)=0xff00u+(i<<16);
    assert(tm_backend_inputs(&b,&inputs)==1);
    for(unsigned i=0;i<16;++i) assert(inputs.keys[i]==0x10203040u+i);
    assert(inputs.mouse==0x00054178 && inputs.wheel==0xfffffffe);
    assert(inputs.horizontal_wheel==0); // stale reserved DDR on legacy FPGA
    REG(m,TM_GEOMETRY_OFFSET+4)=0xdeadbeef;
    assert(tm_backend_inputs(&b,&inputs)==1 && inputs.horizontal_wheel==0);
    REG(m,TM_GEOMETRY_OFFSET+4)=TM_INPUT_EXTENSION_MAGIC;
    assert(tm_backend_inputs(&b,&inputs)==1 && inputs.horizontal_wheel==0xdeadbeef);
    assert(!inputs.mouse_gate);
    REG(m,TM_GEOMETRY_OFFSET+4)=TM_LINUX_INPUT_MAGIC;
    REG(m,TM_MOUSE_OFFSET)=0x800d4178;
    assert(tm_backend_inputs(&b,&inputs)==1 && inputs.mouse_gate==12289);
    assert(!b.linux_wheel); // modeled DDR cannot open physical input devices
    REG(m,TM_GEOMETRY_OFFSET+4)=TM_INPUT_EXTENSION_MAGIC;
    REG(m,TM_MOUSE_OFFSET)=0x00054178;
    assert(tm_backend_inputs(&b,&inputs)==1 && !inputs.mouse_gate);
    for(unsigned i=0;i<4;++i) assert(inputs.joystick[i]==0xff00u+(i<<16));
    REG(m,TM_KEYBOARD_OFFSET)=3;
    REG(m,TM_KEYBOARD_BITS_OFFSET)=0xBAD;
    REG(m,TM_MOUSE_OFFSET)=0xDEAD;
    REG(m,TM_HORIZONTAL_WHEEL_OFFSET)=7;
    assert(tm_backend_inputs(&b,&inputs)==0 && inputs.keys[0]==0x10203040 && inputs.mouse==0x00054178);
    assert(inputs.horizontal_wheel==0xdeadbeef);
    REG(m,TM_KEYBOARD_OFFSET)=0; // even generation wrap is a valid snapshot
    assert(tm_backend_inputs(&b,&inputs)==1 && inputs.keys[0]==0xBAD && inputs.mouse==0xDEAD);
    assert(inputs.horizontal_wheel==7);
    char core_path[]="/tmp/tic80-core-name-XXXXXX";
    int core_fd=mkstemp(core_path);
    assert(core_fd>=0);
    b.core_name_path=core_path;
    name_file(core_fd,"TIC-80",6); assert(tm_backend_core_selected(&b)==1);
    assert(tm_backend_core_generation(&b,&generation)==1);
    assert(tm_backend_core_generation(&b,&next_generation)==1);
    assert(!memcmp(&generation,&next_generation,sizeof generation));
    // Main can rewrite the same bytes in the same second. File identity alone
    // cannot distinguish that completed initialization from the old selection.
    // Some kernels only update inode timestamps once per clock tick. Wait
    // through that granularity rather than assuming a 1 ms rewrite differs.
    for (unsigned attempt=0;attempt<50;++attempt) {
        delay();
        name_file(core_fd,"TIC-80",6);
        assert(tm_backend_core_generation(&b,&next_generation)==1);
        if (memcmp(&generation,&next_generation,sizeof generation)) break;
    }
    assert(generation.device==next_generation.device && generation.inode==next_generation.inode);
    assert(memcmp(&generation,&next_generation,sizeof generation));
    name_file(core_fd,"TIC-80\r\n",8); assert(tm_backend_core_selected(&b)==1);
    name_file(core_fd,"MENU",4); assert(tm_backend_core_selected(&b)==0);
    assert(tm_backend_core_generation(&b,&next_generation)==-1);
    name_file(core_fd,"TIC-80-other",12); assert(tm_backend_core_selected(&b)==0);
    name_file(core_fd,"",0); assert(tm_backend_core_selected(&b)==-1);
    pid_t rewrite=fork(); assert(rewrite>=0);
    if(!rewrite) {
        for(unsigned n=0;n<15;++n) delay();
        name_file(core_fd,"TIC-80",6);
        _exit(0);
    }
    assert(tm_backend_core_selected(&b)==1);
    int rewrite_status;
    assert(waitpid(rewrite,&rewrite_status,0)==rewrite && WIFEXITED(rewrite_status) && !WEXITSTATUS(rewrite_status));
    name_file(core_fd,"TIC-80\0x",8); assert(tm_backend_core_selected(&b)==-1);
    name_file(core_fd,"TIC-80\nMENU",11); assert(tm_backend_core_selected(&b)==-1);
    char too_long[80]; memset(too_long,'X',sizeof too_long);
    name_file(core_fd,too_long,sizeof too_long); assert(tm_backend_core_selected(&b)==-1);
    b.core_name_path="/tmp/tic80-core-name-missing/nonexistent";
    assert(tm_backend_core_selected(&b)==-1);
    b.core_name_path=core_path;
    // Freeze the fixture, retaining perfectly valid stale TIC-80 DDR words.
    // Selection alone must prevent ALL payload/control writes into another core.
    assert(kill(child,SIGSTOP)==0);
    int stopped_status;
    assert(waitpid(child,&stopped_status,WUNTRACED)==child && WIFSTOPPED(stopped_status));
    uint8_t *unchanged=malloc(TM_REGION_BYTES); assert(unchanged);
    memcpy(unchanged,(const void *)m,TM_REGION_BYTES);
    name_file(core_fd,"TIC-80",6);
    tm_exchange before_departure=b.exchange;
    departure_fd=core_fd;
    assert(tm_backend_present_realtime(&b,frame,sizeof frame)==-1);
    assert(departure_fd==-1 && !memcmp(unchanged,(const void *)m,TM_REGION_BYTES));
    b.exchange=before_departure;
    name_file(core_fd,"TIC-80",6);
    int16_t departed_audio[1600]={1,2}; departure_fd=core_fd;
    assert(tm_backend_audio(&b,departed_audio,800)==-1);
    assert(departure_fd==-1 && !memcmp(unchanged,(const void *)m,TM_REGION_BYTES));
    name_file(core_fd,"TIC-80",6);
    puts("Departure after a successful selection read: no video/audio DDR writes passed");
    /* A pending picture must never hold up an urgent streaming audio tick.
     * Keep the entire DDR payload/control region and exchange state intact;
     * finite CLI rendering still requires its acknowledgment. */
    tm_exchange completed_exchange=b.exchange;
    uint32_t completed_audio_write=b.audio_write;
    tm_exchange_init(&b.exchange);
    b.exchange.sequence=1; b.exchange.publication=6; b.exchange.pending=1;
    REG(m,TM_VIDEO_PRESENTED_OFFSET)=0;
    REG(m,TM_VIDEO_PUBLISH_OFFSET)=6;
    b.audio_write=REG(m,TM_AUDIO_READ_OFFSET)+800;
    uint8_t *pending_image=malloc(TM_REGION_BYTES); assert(pending_image);
    memcpy(pending_image,(const void *)m,TM_REGION_BYTES);
    tm_exchange pending_exchange=b.exchange;
    double urgent_at=seconds();
    assert(tm_backend_present_realtime(&b,frame,sizeof frame)==0);
    assert(seconds()-urgent_at<.1);
    assert(!memcmp(&pending_exchange,&b.exchange,sizeof b.exchange));
    assert(!memcmp(pending_image,(const void *)m,TM_REGION_BYTES));
    assert(b.deferred_valid && b.deferred_frame && b.deferred_frame!=frame);
    assert(!memcmp(b.deferred_frame,frame,sizeof frame));
    uint64_t first_wait=b.video_wait_started_ms;
    assert(first_wait);
    assert(tm_backend_present_realtime(&b,frame,sizeof frame)==0);
    assert(b.video_wait_started_ms==first_wait);
    b.video_wait_started_ms=(uint64_t)(seconds()*1000)-1001;
    assert(tm_backend_present_realtime(&b,frame,sizeof frame)==-1);
    assert(!memcmp(pending_image,(const void *)m,TM_REGION_BYTES));
    b.video_wait_started_ms=0;
    urgent_at=seconds();
    assert(tm_backend_present(&b,frame,sizeof frame)==-1);
    assert(seconds()-urgent_at>=.99 && seconds()-urgent_at<1.5);
    assert(!memcmp(pending_image,(const void *)m,TM_REGION_BYTES));
    REG(m,TM_VIDEO_PRESENTED_OFFSET)=99; // stale/foreign ACK is still an error
    assert(tm_backend_present_realtime(&b,frame,sizeof frame)==-1);
    b.video_wait_started_ms=0;
    REG(m,TM_VIDEO_PRESENTED_OFFSET)=0;
    frame[0]^=0xa5;
    assert(tm_backend_present_realtime(&b,frame,sizeof frame)==0);
    uint8_t *latest=malloc(sizeof frame); assert(latest);
    memcpy(latest,frame,sizeof frame);
    frame[1]^=0x55; // a caller may reuse its stack frame immediately
    assert(!memcmp(b.deferred_frame,latest,sizeof frame));
    assert(!memcmp(pending_image,(const void *)m,TM_REGION_BYTES));
    REG(m,TM_VIDEO_PRESENTED_OFFSET)=6;
    assert(tm_backend_pace(&b,NULL)==0);
    assert(!b.deferred_valid && REG(m,TM_VIDEO_PUBLISH_OFFSET)==11);
    assert(!memcmp((const void *)(m+TM_BUFFER1_OFFSET),latest,sizeof frame));
    free(latest);
    frame[0]^=0xa5; frame[1]^=0x55;
    memcpy((void *)m,unchanged,TM_REGION_BYTES);
    b.exchange=completed_exchange; b.audio_write=completed_audio_write;
    b.video_wait_started_ms=0;
    b.deferred_valid=0;
    free(pending_image);
    /* A stopped audio counter must time out without writing DDR. A core
     * departure while pacing must stop promptly rather than wait that timeout. */
    b.audio_write=REG(m,TM_AUDIO_READ_OFFSET)+TM_AUDIO_RESERVE_FRAMES+800;
    b.pacer.audio_clocked=1;
    volatile sig_atomic_t cancel=1;
    double cancelled_at=seconds();
    assert(tm_backend_pace(&b,&cancel)==0 && seconds()-cancelled_at<.1);
    double began=seconds();
    assert(tm_backend_pace(&b,NULL)==-1);
    assert(seconds()-began>=.99 && seconds()-began<1.5);
    assert(!memcmp(unchanged,(const void *)m,TM_REGION_BYTES));
    b.audio_write=REG(m,TM_AUDIO_READ_OFFSET)+4097;
    assert(tm_backend_pace(&b,NULL)==-1);
    b.audio_write=REG(m,TM_AUDIO_READ_OFFSET)+TM_AUDIO_RESERVE_FRAMES+800;
    pid_t pace_switcher=fork(); assert(pace_switcher>=0);
    if(!pace_switcher) {
        for(unsigned n=0;n<20;++n) delay();
        name_file(core_fd,"MENU",4);
        _exit(0);
    }
    began=seconds();
    assert(tm_backend_pace(&b,NULL)==-1 && seconds()-began<.25);
    int pace_status;
    assert(waitpid(pace_switcher,&pace_status,0)==pace_switcher && WIFEXITED(pace_status) && !WEXITSTATUS(pace_status));
    assert(!memcmp(unchanged,(const void *)m,TM_REGION_BYTES));
    name_file(core_fd,"MENU",4);
    int16_t silent[1600]={0};
    assert(tm_backend_audio(&b,silent,800)!=0);
    assert(tm_backend_pace(&b,NULL)!=0);
    assert(tm_backend_present(&b,frame,sizeof frame)!=0);
    assert(tm_backend_inputs(&b,&inputs)==-1);
    assert(tm_backend_cart_pending(&b)==-1);
    assert(tm_backend_core_generation(&b,&next_generation)==-1);
    assert(tm_backend_cart(&b,&cart_copy,&cart_size)==-1);
    assert(tm_backend_restart(&b)!=0);
    tm_backend_close(&b);
    assert(!memcmp(unchanged,(const void *)m,TM_REGION_BYTES));
    free(unchanged);
    // A departure during the handshake must not clear another core's request.
    name_file(core_fd,"TIC-80",6);
    assert(kill(child,SIGCONT)==0);
    assert(tm_backend_open(&b,path)==0);
    assert(kill(child,SIGSTOP)==0);
    assert(waitpid(child,&stopped_status,WUNTRACED)==child && WIFSTOPPED(stopped_status));
    b.core_name_path=core_path;
    uint32_t before_request=REG(m,TM_SESSION_REQUEST_OFFSET);
    pid_t switcher=fork(); assert(switcher>=0);
    if(!switcher) {
        for(unsigned n=0;n<20;++n) delay();
        name_file(core_fd,"MENU",4);
        _exit(0);
    }
    assert(tm_backend_start(&b)!=0);
    int switch_status;
    assert(waitpid(switcher,&switch_status,0)==switcher && WIFEXITED(switch_status) && !WEXITSTATUS(switch_status));
    uint32_t left_request=REG(m,TM_SESSION_REQUEST_OFFSET);
    assert(left_request && left_request!=before_request);
    tm_backend_close(&b);
    assert(REG(m,TM_SESSION_REQUEST_OFFSET)==left_request);
    name_file(core_fd,"TIC-80",6);
    assert(kill(child,SIGCONT)==0);
    assert(tm_backend_open(&b,path)==0 && tm_backend_start(&b)==0);
    close(core_fd); unlink(core_path);
    tm_backend_close(&b);
    assert(REG(m, TM_SESSION_REQUEST_OFFSET) == 0);
    assert(tm_backend_open(&b, path) == 0 && tm_backend_start(&b) == 0);
    REG(m, TM_SESSION_REQUEST_OFFSET) = b.session + 1; // replacement session
    uint32_t replacement = REG(m, TM_SESSION_REQUEST_OFFSET);
    tm_backend_close(&b);
    assert(REG(m, TM_SESSION_REQUEST_OFFSET) == replacement);
    /* A moving, geometrically compatible TIC2 core must not accept a TIC3
     * producer: its pixel stride and later payload offsets are different. */
    uint32_t old_write=REG(m,TM_AUDIO_WRITE_OFFSET), old_publish=REG(m,TM_VIDEO_PUBLISH_OFFSET);
    uint32_t old_ack=REG(m,TM_CART_ACK_OFFSET), heartbeat=REG(m,TM_HEARTBEAT_OFFSET);
    REG(m,TM_IDENTITY_OFFSET)=0x32434954u;
    assert(tm_backend_open(&b,path) != 0);
    assert(REG(m,TM_HEARTBEAT_OFFSET) != heartbeat);
    assert(REG(m,TM_SESSION_REQUEST_OFFSET)==replacement);
    assert(REG(m,TM_AUDIO_WRITE_OFFSET)==old_write && REG(m,TM_VIDEO_PUBLISH_OFFSET)==old_publish);
    assert(REG(m,TM_CART_ACK_OFFSET)==old_ack);
    REG(m,TM_IDENTITY_OFFSET)=TM_MAGIC;
    assert(kill(child, SIGTERM) == 0);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status));
    uint32_t request = REG(m, TM_SESSION_REQUEST_OFFSET);
    assert(tm_backend_open(&b, path) != 0); /* identity alone is insufficient */
    assert(REG(m, TM_SESSION_REQUEST_OFFSET) == request);
    munmap((void *)m, TM_REGION_BYTES);
    close(fd);
    unlink(path);
    puts("Live transport: identity, heartbeat, core departure without DDR writes, restart, 32 payloads and four-pad mapping passed");
    return 0;
}
