#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/vm.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include "core/core.h"
#include "tools.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "sfx_notes_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"SFX_NOTES_FAILED line=%d expression=%s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned calls,errors;
static char error_text[2048];
void __real_tic_api_sfx(tic_mem*,s32,s32,s32,s32,s32,s32,s32,s32);
void __wrap_tic_api_sfx(tic_mem *tic,s32 index,s32 note,s32 octave,s32 duration,s32 channel,s32 left,s32 right,s32 speed)
{
    if(index>=SFX_COUNT || channel<0 || channel>=TIC_SOUND_CHANNELS) {
        fprintf(stderr,"SFX_CORE_BOUNDARY index=%d channel=%d\n",index,channel); _exit(42);
    }
    ++calls; __real_tic_api_sfx(tic,index,note,octave,duration,channel,left,right,speed);
}
static void error(const char *text)
{
    ++errors; size_t used=strlen(error_text);
    snprintf(error_text+used,sizeof error_text-used,"%s\n",text);
}
static void reference_error(void *data,const char *text) { (void)data; error(text); }
static u64 counter(void *data) { (void)data; return 0; }
static u64 frequency(void *data) { (void)data; return 60; }
static const tic_script *language(const char *name)
{ FOREACH_LANG(s) if(!strcmp(name,s->name)) return s; CHECK(0); return NULL; }
static u8 *encode(tic_cartridge *cart,s32 *size)
{ u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes); *size=tic_cart_save(cart,bytes); CHECK(*size>0); return bytes; }
static void run_case(unsigned number)
{
    const typeof(*note_cases) *test=note_cases+number;
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart); cart->lang=language(test->language)->id;
    strcpy(cart->code.data,test->source);
    for(unsigned effect=0;effect<SFX_COUNT;++effect) {
        tic_sample *sample=cart->banks[0].sfx.samples.data+effect; sample->note=0; sample->octave=5;
        for(unsigned step=0;step<SFX_TICKS;++step) sample->data[step].wave=effect%2;
    }
    for(unsigned wave=0;wave<2;++wave) for(unsigned x=0;x<WAVE_VALUES;++x)
        tic_tool_poke4(cart->banks[0].sfx.waveforms.items[wave].data,x,(x*(wave+1))%16);
    s32 size; u8 *bytes=encode(cart,&size);
    tic80 *actual=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(actual); actual->callback.error=error;
    tic80_load(actual,bytes,size); calls=errors=0; error_text[0]=0;
    // The engine reference uses explicit pitches independently of generated strings.
    static const int pitches[]={36,37,38,39,40,41,42,43,44,45,46,47,-11,0,95};
    tic80 *reference=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(reference);
    memset(cart->code.data,0,sizeof cart->code.data); cart->lang=language("lua")->id;
    strcpy(cart->code.data,"function TIC() end\n");
    s32 ref_size; u8 *ref_bytes=encode(cart,&ref_size); tic80_load(reference,ref_bytes,ref_size); free(ref_bytes);
    tic_core *ref=(tic_core*)reference;
    *ref->data=(tic_tick_data){.counter=counter,.freq=frequency,.error=reference_error};
    s16 audio[2][1600];
    for(unsigned frame=0;frame<2;++frame) {
        tic80_tick(actual,(tic80_input){0},counter,frequency); tic80_sound(actual);
        CHECK(actual->samples.count==1600); memcpy(audio[frame],actual->samples.buffer,sizeof audio[frame]);
        bool expected_error=frame && test->error;
        if((errors>0)!=expected_error)
            fprintf(stderr,"SFX_NOTES_ERROR_STATE case=%s frame=%u errors=%u expected=%u calls=%u text=%s\n",test->name,frame,errors,frame&&test->error?1u:0u,calls,error_text);
        CHECK((errors>0)==expected_error);
        CHECK(calls==1+(frame && !test->error));
        if(frame && test->error) CHECK(strstr(error_text,"invalid note"));
        tic_mem *ref_mem=(tic_mem*)reference;
        ref_mem->ram->input=(tic80_input){0}; tic_core_tick_start(ref_mem); tic_core_tick(ref_mem,ref->data);
        if(!frame) __real_tic_api_sfx((tic_mem*)reference,0,0,5,-1,3,15,15,0);
        else if(!test->error) {
            int pitch=pitches[test->scenario];
            __real_tic_api_sfx((tic_mem*)reference,0,pitch%12,pitch/12,-1,3,15,15,0);
        }
        tic_core_tick_end(ref_mem); tic_core_blit(ref_mem); tic80_sound(reference); CHECK(reference->samples.count==1600);
        tic_channel_data *got=((tic_core*)actual)->state.sfx.channels+3;
        tic_channel_data *want=((tic_core*)reference)->state.sfx.channels+3;
        if(got->index!=want->index || got->note!=want->note || got->speed!=want->speed || got->duration!=want->duration)
            fprintf(stderr,"SFX_NOTES_STATE case=%s frame=%u note=%d/%d index=%d/%d\n",test->name,frame,got->note,want->note,got->index,want->index);
        CHECK(got->index==want->index && got->note==want->note && got->speed==want->speed && got->duration==want->duration);
        CHECK(!memcmp(actual->samples.buffer,reference->samples.buffer,sizeof audio[frame]));
        CHECK(tic_api_pmem((tic_mem*)actual,31,0,false)==frame+1);
    }
    tic80_delete(actual); tic80_delete(reference);
    tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
    memset(cart,0,sizeof *cart); cart->lang=language("lua")->id;
    strcpy(cart->code.data,"function TIC() pmem(30,pmem(30)+1) end\n"); bytes=encode(cart,&size); free(cart);
    tm_vm *peer=NULL; CHECK(tm_vm_open(&peer,bytes,size)==TM_VM_OK); free(bytes);
    for(unsigned frame=0;frame<2;++frame) {
        int result=tm_vm_tick(vm,(tic80_input){0},0);
        CHECK(result==(frame && test->error?TM_VM_ERROR:TM_VM_OK));
        tic80 *product=tm_vm_product(vm); CHECK(product->samples.count==1600);
        CHECK(!memcmp(product->samples.buffer,audio[frame],sizeof audio[frame]));
        CHECK(tic_api_pmem((tic_mem*)product,31,0,false)==frame+1);
        CHECK(tm_vm_tick(peer,(tic80_input){0},0)==TM_VM_OK);
        CHECK(tic_api_pmem((tic_mem*)tm_vm_product(peer),30,0,false)==frame+1);
    }
    tm_vm_close(vm); tm_vm_close(peer);
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    printf("SFX_NOTES_PASS case=%s scenario=%u error=%u exact_pcm=1 peer_unaffected=1\n",test->name,test->scenario,test->error);
}
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1 || (argc==3 && !strcmp(argv[1],"--case")));
    CHECK(!setvbuf(stdout,NULL,_IOLBF,0)); unsigned tested=0;
    struct rlimit cores={0,0}; CHECK(!setrlimit(RLIMIT_CORE,&cores));
    for(unsigned c=0;c<sizeof note_cases/sizeof *note_cases;++c) {
        if(argc==3 && strcmp(argv[2],note_cases[c].name)) continue;
        printf("SFX_NOTES_START case=%s\n",note_cases[c].name);
        pid_t child=fork(); CHECK(child>=0);
        if(!child) { run_case(c); exit(0); }
        int status; pid_t waited;
        do waited=waitpid(child,&status,0); while(waited<0 && errno==EINTR);
        CHECK(waited==child);
        if(!WIFEXITED(status) || WEXITSTATUS(status))
            fprintf(stderr,"SFX_NOTES_CHILD_FAILURE case=%s signal=%d exit=%d\n",note_cases[c].name,WIFSIGNALED(status)?WTERMSIG(status):0,WIFEXITED(status)?WEXITSTATUS(status):-1);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0); ++tested;
    }
    CHECK(tested==(argc==1?288u:1u));
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    printf("SFX notes contract: %u cases, valid pitch equivalence, error without engine call, exact C/direct/worker PCM and independent peer passed\n",tested);
    return 0;
}
