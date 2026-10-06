#include "tic80_mister/vm.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_api_cases.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"input failed line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
_Static_assert(tic_key_a==1 && tic_key_w==23 && tic_key_q==17 && tic_key_e==5, "letter input fixture ABI");
_Static_assert(tic_key_return==50 && tic_key_escape==66 && tic_key_ctrl==63 && tic_key_shift==64, "control input fixture ABI");
_Static_assert(tic_key_f12==78 && tic_key_numpadperiod==94, "high input fixture ABI");
static const tic_key keys[]={tic_key_a,tic_key_w,tic_key_q,tic_key_e,tic_key_return,
    tic_key_escape,tic_key_ctrl,tic_key_shift,tic_key_f12,tic_key_numpadperiod};
static unsigned errors;
static u64 ticks;
static void error(const char *text) { fprintf(stderr,"input runtime error: %s\n",text); ++errors; }
static u64 counter(void *data) { (void)data; return ticks; }
static u64 frequency(void *data) { (void)data; return 60; }
static const tic_script *language(const char *name)
{ FOREACH_LANG(s) if (!strcmp(s->name,name)) return s; CHECK(0); return NULL; }

typedef struct { tic80 *direct; tm_vm *worker; unsigned frames; } pair;
static pair open_pair(const u8 *bytes,s32 size)
{
    pair p={0};
    CHECK(tm_vm_open(&p.worker,bytes,(size_t)size)==TM_VM_OK);
    p.direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(p.direct);
    p.direct->callback.error=error; tic80_load(p.direct,(void*)bytes,size); return p;
}
static void close_pair(pair *p)
{ tic80_delete(p->direct); tm_vm_close(p->worker); memset(p,0,sizeof *p); }

/* Independently specified event schedule: controller and keyboard edges differ.
 * The walking-bit tail exercises every action on all four gamepads separately. */
static tic80_input input(unsigned frame,unsigned peer)
{
    tic80_input i={0};
    if (frame>=1 && frame<=10) i.gamepads.data=peer?0x5a493827u:0xa5b6c7d8u;
    else if (frame==12) i.gamepads.data=peer?0xa5b6c7d8u:0x5a493827u;
    else if (frame>=16 && !(frame&1)) i.gamepads.data=1u<<((frame-16)/2);
    if ((frame>=2 && frame<=10) || frame==12) {
        i.keyboard.keys[0]=peer?tic_key_e:tic_key_a;
        i.keyboard.keys[1]=peer?tic_key_return:tic_key_f12;
        i.keyboard.keys[2]=peer?tic_key_escape:tic_key_numpadperiod;
        i.keyboard.keys[3]=peer?tic_key_ctrl:tic_key_shift;
    }
    return i;
}

static void check(tic80 *tic,const char *name,unsigned frame,unsigned peer,unsigned count,unsigned restarted)
{
    tic80_input i=input(frame,peer);
    /* A replacement is opened before frame 8, while the old input was held.
     * Its first tick must see a new press; its peer retains the old history. */
    unsigned button_edge=frame==1 || frame==12 || (frame>=16 && !(frame&1)) || (restarted && frame==8);
    unsigned key_edge=frame==2 || frame==12 || (restarted && frame==8);
    unsigned button_repeat=button_edge || (!restarted && (frame==5 || frame==7 || frame==9));
    unsigned key_repeat=key_edge || (!restarted && (frame==6 || frame==8 || frame==10));
    unsigned expected[129]={0};
    for (unsigned bit=0;bit<32;++bit) {
        expected[bit]=(i.gamepads.data>>bit)&1u;
        expected[32+bit]=expected[bit] && button_edge;
        expected[64+bit]=expected[bit] && button_repeat;
    }
    for (unsigned key=0;key<sizeof keys/sizeof *keys;++key) {
        for (unsigned n=0;n<TIC80_KEY_BUFFER;++n) if (i.keyboard.keys[n]==keys[key]) expected[96+key]=1;
        expected[106+key]=expected[96+key] && key_edge;
        expected[116+key]=expected[96+key] && key_repeat;
    }
    expected[127]=count;
    expected[126]=i.keyboard.data!=0;
    expected[128]=expected[126] && key_edge;
    for (unsigned slot=0;slot<129;++slot) {
        unsigned got=tic_api_pmem((tic_mem*)tic,slot,0,false);
        if (got!=expected[slot]) fprintf(stderr,"INPUT_STATE language=%s frame=%u peer=%u restarted=%u slot=%u got=%u expected=%u\n",
            name,frame,peer,restarted,slot,got,expected[slot]);
        CHECK(got==expected[slot]);
    }
    CHECK(tic->samples.count==1600);
}

int main(int argc,char **argv)
{
    if (argc>=2 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    const char *only=NULL;
    if (argc==3 && !strcmp(argv[1],"--language")) only=argv[2];
    else CHECK(argc==1);
    setvbuf(stdout,NULL,_IONBF,0); unsigned languages=0;
    for (unsigned c=0;c<sizeof input_cases/sizeof *input_cases;++c) {
        const char *name=input_cases[c].language;
        if (only && strcmp(only,name)) continue;
        ++languages;
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
        CHECK(strlen(input_cases[c].source)<sizeof cart->code.data); strcpy(cart->code.data,input_cases[c].source);
        if (input_cases[c].binary) {
            CHECK(input_cases[c].size<sizeof cart->binary.data);
            memcpy(cart->binary.data,input_cases[c].binary,input_cases[c].size); cart->binary.size=input_cases[c].size;
        }
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0); free(cart);
        pair peers[]={open_pair(bytes,size),open_pair(bytes,size)};
        unsigned restarted=0;
        for (unsigned frame=0;frame<80;++frame) {
            if (frame==8) { close_pair(peers); peers[0]=open_pair(bytes,size); restarted=1; }
            for (unsigned peer=0;peer<2;++peer) {
                ++ticks; tic80_input i=input(frame,peer);
                tic80_tick(peers[peer].direct,i,counter,frequency); tic80_sound(peers[peer].direct); CHECK(!errors);
                CHECK(tm_vm_tick(peers[peer].worker,i,0)==TM_VM_OK); ++peers[peer].frames;
                check(peers[peer].direct,name,frame,peer,peers[peer].frames,restarted && !peer);
                check(tm_vm_product(peers[peer].worker),name,frame,peer,peers[peer].frames,restarted && !peer);
            }
        }
        close_pair(peers); close_pair(peers+1); free(bytes);
        printf("%s: four-gamepad walking bits, held/edge/repeat, keyboard independence and replacement/peer isolation passed\n",name);
    }
    CHECK(languages==(only?1:14));
    printf("%u runtime input contracts passed through direct and supervised players\n",languages); return 0;
}
