#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/vm.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "core/core.h"
#include "cart.h"
#include "script.h"
#include "tools.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "sfx_defaults_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"SFX defaults failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned errors;
static const tic_script *language(const char *name)
{ FOREACH_LANG(s) if(!strcmp(s->name,name)) return s; CHECK(0); return NULL; }
static u64 counter(void *data) { (void)data; return 1; }
static u64 frequency(void *data) { (void)data; return 60; }
static void error(const char *text) { fprintf(stderr,"SFX defaults runtime error: %s\n",text); ++errors; }
static void reference_error(void *data,const char *text) { (void)data; error(text); }
/* Explicit engine arguments: independently specified, never read from generated calls. */
typedef struct { int index,note,octave,duration,channel,left,right,speed; } action;
static const action expected[]={
    {0,7,2,-1,0,15,15,-2}, {1,2,4,-1,0,15,15,1}, {63,1,7,-1,0,15,15,3},
    {0,7,2,6,0,15,15,-2}, {63,1,7,6,0,15,15,3}, {0,0,4,-1,0,15,15,-2},
    {0,7,2,-1,2,15,15,0}, {0,0,5,8,1,5,5,3}, {0,7,2,7,3,0,15,-2},
    {-1,-1,-1,-1,0,15,15,0}
};
static void execute(tic_mem *mem,const action *a)
{ tic_api_sfx(mem,a->index,a->note,a->octave,a->duration,a->channel,a->left,a->right,a->speed); }
static void reference_tick(tic80 *tic,unsigned frame,unsigned scenario)
{
    tic_mem *mem=(tic_mem*)tic; tic_core *core=(tic_core*)tic;
    mem->ram->input=(tic80_input){0}; tic_core_tick_start(mem); tic_core_tick(mem,core->data);
    if(frame==0) execute(mem,expected+(scenario==9?0:scenario));
    if(frame==2 && scenario==9) execute(mem,expected+9);
    if(frame==12) for(int channel=0;channel<4;++channel) tic_api_sfx(mem,-1,0,0,0,channel,0,0,0);
    tic_api_pmem(mem,31,frame+1,true); tic_core_tick_end(mem); tic_core_blit(mem); tic80_sound(tic);
}
static void seed(tic_cartridge *cart)
{
    for(unsigned wave=0;wave<4;++wave) for(unsigned x=0;x<WAVE_VALUES;++x)
        tic_tool_poke4(cart->banks[0].sfx.waveforms.items[wave].data,x,(x*(wave+1)+wave*3)%16);
    const unsigned indices[]={0,1,63};
    for(unsigned i=0;i<3;++i) {
        tic_sample *s=cart->banks[0].sfx.samples.data+indices[i];
        s->note=expected[i].note; s->octave=expected[i].octave; s->speed=expected[i].speed;
        for(unsigned step=0;step<SFX_TICKS;++step) { s->data[step].wave=(step+i)%4; s->data[step].volume=step%5; }
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
    CHECK(argc==1 || (argc==3 && !strcmp(argv[1],"--case")));
    CHECK(!setvbuf(stdout,NULL,_IOLBF,0)); unsigned cases=0;
    for(unsigned c=0;c<sizeof defaults_cases/sizeof *defaults_cases;++c) {
        const char *name=defaults_cases[c].language,*test=defaults_cases[c].name;
        char id[64]; snprintf(id,sizeof id,"%s-%s",name,test);
        if(argc==3 && strcmp(id,argv[2])) continue;
        unsigned scenario=defaults_cases[c].scenario;
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); seed(cart); cart->lang=language(name)->id;
        strcpy(cart->code.data,defaults_cases[c].source); tic80 *actual=player(cart);
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
        memset(&cart->code,0,sizeof cart->code); cart->lang=language("lua")->id;
        strcpy(cart->code.data,"function TIC() end\n"); tic80 *reference=player(cart); free(cart);
        tic_core *ref=(tic_core*)reference; *ref->data=(tic_tick_data){.counter=counter,.freq=frequency,.error=reference_error};
        unsigned nonzero=0; printf("SFX_DEFAULTS_START case=%s\n",id);
        for(unsigned frame=0;frame<24;++frame) {
            tic80_tick(actual,(tic80_input){0},counter,frequency); tic80_sound(actual); CHECK(!errors);
            reference_tick(reference,frame,scenario); CHECK(!errors); tic_core *a=(tic_core*)actual;
            for(unsigned channel=0;channel<4;++channel) {
                tic_channel_data *got=a->state.sfx.channels+channel,*want=ref->state.sfx.channels+channel;
                if(got->index!=want->index || got->speed!=want->speed || (got->index>=0 && got->note!=want->note) || got->duration!=want->duration || got->volume.left!=want->volume.left || got->volume.right!=want->volume.right)
                    fprintf(stderr,"SFX_DEFAULTS_STATE case=%s frame=%u channel=%u index=%d/%d speed=%d/%d note=%d/%d duration=%d/%d volume=%u,%u/%u,%u\n",id,frame,channel,got->index,want->index,got->speed,want->speed,got->note,want->note,got->duration,want->duration,got->volume.left,got->volume.right,want->volume.left,want->volume.right);
                CHECK(got->index==want->index && got->speed==want->speed && (got->index<0 || got->note==want->note) && got->duration==want->duration && got->volume.left==want->volume.left && got->volume.right==want->volume.right);
            }
            CHECK(actual->samples.count==1600 && reference->samples.count==1600);
            CHECK(!memcmp(actual->samples.buffer,reference->samples.buffer,1600*sizeof(s16)));
            CHECK(tic_api_pmem((tic_mem*)actual,31,0,false)==frame+1);
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); tic80 *product=tm_vm_product(vm);
            CHECK(product->samples.count==1600 && !memcmp(product->samples.buffer,reference->samples.buffer,1600*sizeof(s16)));
            CHECK(tic_api_pmem((tic_mem*)product,31,0,false)==frame+1);
            for(unsigned sample=0;sample<1600;++sample) nonzero+=reference->samples.buffer[sample]!=0;
        }
        CHECK(nonzero>0); tm_vm_close(vm); tic80_delete(actual); tic80_delete(reference);
        CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD); ++cases;
        printf("SFX_DEFAULTS_PASS case=%s frames=24 samples=38400 nonzero=%u exact_pcm=1\n",id,nonzero);
    }
    CHECK(cases==(argc==1?50:1));
    printf("SFX defaults contract: %u cases, exact direct/worker stereo PCM, preset pitch/speed, omitted and overridden arguments, stops passed\n",cases);
    return 0;
}
