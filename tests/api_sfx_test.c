#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/vm.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "core/core.h"
#include "cart.h"
#include "script.h"
#include "tools.h"
#include "wren.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "sfx_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"SFX failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned slot_reads,errors;
void __real_wrenGetListElement(WrenVM*,int,int,int);
void __wrap_wrenGetListElement(WrenVM *vm,int list,int index,int destination)
{
    int count=wrenGetSlotCount(vm);
    if(destination<0 || destination>=count) {
        fprintf(stderr,"WREN_SLOT_CONTRACT destination=%d slots=%d\n",destination,count); exit(1);
    }
    ++slot_reads; __real_wrenGetListElement(vm,list,index,destination);
}
static const tic_script *language(const char *name)
{ FOREACH_LANG(s) if(!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
static u64 counter(void *data) { (void)data; return 1; }
static u64 frequency(void *data) { (void)data; return 60; }
static void error(const char *text) { fprintf(stderr,"SFX runtime error: %s\n",text); ++errors; }
static void reference_error(void *data,const char *text) { (void)data; error(text); }
typedef struct { unsigned frame; int index,note,duration,channel,volume,speed; } action;
static const action plan[]={
    {0,0,60,8,2,15,3},{2,1,50,10,0,5,-2},{5,-1,0,0,2,0,0},
    {7,0,31,6,1,7,1},{9,-1,0,0,0,0,0},{13,1,48,-1,3,15,0},
    {14,-1,0,0,1,0,0},{18,-1,0,0,0,0,0},{18,-1,0,0,1,0,0},
    {18,-1,0,0,2,0,0},{18,-1,0,0,3,0,0}};
static void reference_tick(tic80 *tic,unsigned frame,unsigned variant)
{
    tic_mem *mem=(tic_mem*)tic; tic_core *core=(tic_core*)tic;
    mem->ram->input=(tic80_input){0}; tic_core_tick_start(mem); tic_core_tick(mem,core->data);
    if(!variant) {
        for(unsigned i=0;i<sizeof plan/sizeof *plan;++i) if(plan[i].frame==frame) {
            const action *a=plan+i; tic_api_sfx(mem,a->index,a->note%12,a->note/12,a->duration,a->channel,a->volume,a->volume,a->speed);
        }
    } else {
        if(frame==0) tic_api_sfx(mem,0,0,4,8,2,0,15,variant==1?-2:3);
        if(frame==6) tic_api_sfx(mem,1,2,4,10,0,15,0,variant==1?1:-1);
        if(frame==12) tic_api_sfx(mem,-1,0,0,0,2,0,0,0);
        if(frame==14) tic_api_sfx(mem,-1,0,0,0,0,0,0,0);
    }
    tic_api_pmem(mem,31,frame+1,true); tic_core_tick_end(mem); tic_core_blit(mem); tic80_sound(tic);
}
static void seed(tic_cartridge *cart)
{
    for(unsigned wave=0;wave<4;++wave) for(unsigned x=0;x<WAVE_VALUES;++x)
        tic_tool_poke4(cart->banks[0].sfx.waveforms.items[wave].data,x,(x*(wave+1)+wave*3)%16);
    for(unsigned sample=0;sample<2;++sample) {
        tic_sample *s=cart->banks[0].sfx.samples.data+sample; s->note=sample?2:7; s->octave=sample?4:2; s->speed=sample?1:-2;
        for(unsigned step=0;step<SFX_TICKS;++step) { s->data[step].wave=(step+sample)%4; s->data[step].volume=step%5; }
    }
}
static tic80 *player(tic_cartridge *cart)
{
    u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(tic); tic->callback.error=error; tic80_load(tic,bytes,size); free(bytes); return tic;
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1 || (argc==3 && !strcmp(argv[1],"--language")) || (argc==2 && !strcmp(argv[1],"--wren-stereo")));
    CHECK(!setvbuf(stdout,NULL,_IOLBF,0)); unsigned cases=0;
    for(unsigned c=0;c<sizeof sfx_cases/sizeof *sfx_cases;++c) {
        const char *name=sfx_cases[c].language; unsigned variant=sfx_cases[c].variant;
        if(argc==3 && (variant || strcmp(name,argv[2]))) continue;
        if(argc==2 && !variant) continue;
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); seed(cart); cart->lang=language(name)->id;
        strcpy(cart->code.data,sfx_cases[c].source);
        if(sfx_cases[c].binary) { memcpy(cart->binary.data,sfx_cases[c].binary,sfx_cases[c].size); cart->binary.size=sfx_cases[c].size; }
        tic80 *actual=player(cart);
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        memset(&cart->code,0,sizeof cart->code); memset(&cart->binary,0,sizeof cart->binary);
        cart->lang=language("lua")->id; strcpy(cart->code.data,"function TIC() end\n"); tic80 *reference=player(cart); free(cart);
        tic_core *ref=(tic_core*)reference; *ref->data=(tic_tick_data){.counter=counter,.freq=frequency,.error=reference_error};
        unsigned reads_before=slot_reads,nonzero=0;
        printf("API_SFX_START language=%s variant=%u\n",name,variant);
        for(unsigned frame=0;frame<36;++frame) {
            tic80_tick(actual,(tic80_input){0},counter,frequency); tic80_sound(actual); CHECK(!errors);
            reference_tick(reference,frame,variant); CHECK(!errors);
            tic_core *a=(tic_core*)actual;
            for(unsigned channel=0;channel<4;++channel) {
                tic_channel_data *got=a->state.sfx.channels+channel,*want=ref->state.sfx.channels+channel;
                // Bindings may leave different unused pitch values when id=-1
                // stops a channel. Require the pitch only for an active SFX.
                if(got->index!=want->index || got->speed!=want->speed || (got->index>=0 && got->note!=want->note) || got->duration!=want->duration ||
                   got->volume.left!=want->volume.left || got->volume.right!=want->volume.right)
                    fprintf(stderr,"API_SFX_STATE language=%s variant=%u frame=%u channel=%u index=%d/%d speed=%d/%d note=%d/%d duration=%d/%d volume=%u,%u/%u,%u\n",name,variant,frame,channel,got->index,want->index,got->speed,want->speed,got->note,want->note,got->duration,want->duration,got->volume.left,got->volume.right,want->volume.left,want->volume.right);
                CHECK(got->index==want->index && got->speed==want->speed && (got->index<0 || got->note==want->note) && got->duration==want->duration && got->volume.left==want->volume.left && got->volume.right==want->volume.right);
            }
            CHECK(actual->samples.count==1600 && reference->samples.count==1600);
            CHECK(!memcmp(actual->samples.buffer,reference->samples.buffer,1600*sizeof(s16)));
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
            tic80 *product=tm_vm_product(vm); CHECK(product->samples.count==1600);
            CHECK(!memcmp(product->samples.buffer,reference->samples.buffer,1600*sizeof(s16)));
            CHECK(tic_api_pmem((tic_mem*)product,31,0,false)==frame+1);
            for(unsigned sample=0;sample<1600;++sample) nonzero+=reference->samples.buffer[sample]!=0;
        }
        CHECK(nonzero>0); if(variant) CHECK(slot_reads-reads_before==8);
        tm_vm_close(vm); tic80_delete(actual); tic80_delete(reference);
        CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD); ++cases;
        printf("API_SFX_PASS language=%s variant=%u frames=36 samples=57600 nonzero=%u slot_reads=%u\n",name,variant,nonzero,slot_reads-reads_before);
    }
    CHECK(cases==(argc==1?16:argc==2?2:1));
    printf("SFX C-reference contract: %u cases, exact direct/worker stereo PCM, explicit pitch/speed/duration/channel/volume, stop and Wren slot ownership passed\n",cases);
    return 0;
}
