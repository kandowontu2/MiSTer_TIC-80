#include "tic80_mister/vm.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mouse_api_cases.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"mouse failed line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
_Static_assert(TIC80_OFFSET_LEFT==8 && TIC80_OFFSET_TOP==4,"Mouse cartridge coordinates exclude the border");
static unsigned errors;
static u64 ticks;
static void error(const char *text) { fprintf(stderr,"mouse runtime error: %s\n",text); ++errors; }
static u64 counter(void *data) { (void)data; return ticks; }
static u64 frequency(void *data) { (void)data; return 60; }
static const tic_script *language(const char *name)
{ FOREACH_LANG(s) if (!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
typedef struct { tic80 *direct; tm_vm *worker; unsigned frames; } pair;
static pair open_pair(const u8 *bytes,s32 size)
{
    pair p={0}; CHECK(tm_vm_open(&p.worker,bytes,(size_t)size)==TM_VM_OK);
    p.direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(p.direct);
    p.direct->callback.error=error; tic80_load(p.direct,(void*)bytes,size); return p;
}
static void close_pair(pair *p)
{ tic80_delete(p->direct); tm_vm_close(p->worker); memset(p,0,sizeof *p); }
static const int positions[]={0,1,8,4,127,128,239,255};
static const int wheels[]={-32,-31,-1,0,1,15,30,31};
static tic80_input input(unsigned frame,unsigned peer)
{
    tic80_input i={0}; unsigned n=(frame+peer*3)%8;
    i.mouse.relative=frame&1;
    if (i.mouse.relative) { i.mouse.rx=(s8)(positions[n]-128); i.mouse.ry=(s8)(positions[7-n]-128); }
    else { i.mouse.x=(u8)positions[n]; i.mouse.y=(u8)positions[7-n]; }
    unsigned buttons=(frame+peer*5)%8;
    i.mouse.left=buttons&1; i.mouse.middle=(buttons>>1)&1; i.mouse.right=(buttons>>2)&1;
    i.mouse.scrollx=wheels[n]; i.mouse.scrolly=wheels[7-n]; return i;
}
static void check(tic80 *tic,const char *name,unsigned frame,unsigned peer,unsigned count)
{
    unsigned n=(frame+peer*3)%8,buttons=(frame+peer*5)%8;
    /* Expectations use the API's absolute-border and signed-relative rules,
     * not tic_api_mouse() or the scripting binding's returned object. */
    int expected[]={positions[n]-(frame&1?128:8),positions[7-n]-(frame&1?128:4),
        (int)(buttons&1),(int)((buttons>>1)&1),(int)((buttons>>2)&1),wheels[n],wheels[7-n]};
    for(unsigned query=0;query<2;++query) for(unsigned field=0;field<7;++field) {
        unsigned slot=query*16+field; u32 got=tic_api_pmem((tic_mem*)tic,slot,0,false);
        if (got!=(u32)expected[field]) fprintf(stderr,"MOUSE_STATE language=%s frame=%u peer=%u query=%u field=%u got=%d expected=%d\n",
            name,frame,peer,query,field,(s32)got,expected[field]);
        CHECK(got==(u32)expected[field]);
    }
    CHECK(tic_api_pmem((tic_mem*)tic,7,0,false)==count); CHECK(tic->samples.count==1600);
}
int main(int argc,char **argv)
{
    if (argc>=2 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    const char *only=NULL;
    if (argc==3 && !strcmp(argv[1],"--language")) only=argv[2]; else CHECK(argc==1);
    setvbuf(stdout,NULL,_IONBF,0); unsigned languages=0;
    for(unsigned c=0;c<sizeof mouse_cases/sizeof *mouse_cases;++c) {
        const char *name=mouse_cases[c].language; if (only && strcmp(only,name)) continue; ++languages;
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(name)->id;
        CHECK(strlen(mouse_cases[c].source)<sizeof cart->code.data); strcpy(cart->code.data,mouse_cases[c].source);
        if(mouse_cases[c].binary) { CHECK(mouse_cases[c].size<sizeof cart->binary.data);
            memcpy(cart->binary.data,mouse_cases[c].binary,mouse_cases[c].size); cart->binary.size=mouse_cases[c].size; }
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0); free(cart);
        pair peers[]={open_pair(bytes,size),open_pair(bytes,size)};
        for(unsigned frame=0;frame<32;++frame) {
            if(frame==9) { close_pair(peers); peers[0]=open_pair(bytes,size); }
            for(unsigned peer=0;peer<2;++peer) {
                ++ticks; tic80_input i=input(frame,peer);
                tic80_tick(peers[peer].direct,i,counter,frequency); tic80_sound(peers[peer].direct); CHECK(!errors);
                CHECK(tm_vm_tick(peers[peer].worker,i,0)==TM_VM_OK); ++peers[peer].frames;
                check(peers[peer].direct,name,frame,peer,peers[peer].frames);
                check(tm_vm_product(peers[peer].worker),name,frame,peer,peers[peer].frames);
            }
        }
        close_pair(peers); close_pair(peers+1); free(bytes);
        printf("%s: mouse absolute/relative coordinates, all button masks, signed wheels, repeat queries and replacement/peer isolation passed\n",name);
    }
    CHECK(languages==(only?1:14)); printf("%u runtime mouse contracts passed through direct and supervised players\n",languages); return 0;
}
