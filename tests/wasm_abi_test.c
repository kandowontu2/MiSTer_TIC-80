#include "tic80.h"
#include "tic.h"
#include "cart.h"
#include "script.h"
#include "core/core.h"
#include "wasm3.h"
#include "tic80_mister/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm_abi_case.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned errors, mismatches;
static void error(const char *text) { fprintf(stderr,"TIC-80: %s\n",text); ++errors; }
static u64 counter(void *data) { (void)data; return 0; }
static u64 frequency(void *data) { (void)data; return 60; }
static IM3Function find(IM3Runtime runtime,const char *name)
{
    IM3Function fun=NULL; CHECK(!m3_FindFunction(&fun,runtime,name) && fun); return fun;
}
static void poison(IM3Function pmem,uint32_t value)
{
    /* Both calls are ordinary API calls. Reading the stored full-width value
     * seeds the next result slot; no interpreter memory is modified by the test. */
    CHECK(!m3_CallV(pmem,(int32_t)255,(int64_t)value));
    CHECK(!m3_CallV(pmem,(int32_t)255,(int64_t)-1));
    uint32_t actual=0; CHECK(!m3_GetResultsV(pmem,&actual)); CHECK(actual==value);
}
static void expect(IM3Function fun,const char *name,uint32_t expected,uint32_t seed)
{
    uint32_t actual=0; CHECK(!m3_GetResultsV(fun,&actual));
    if(actual!=expected) {
        fprintf(stderr,"%s after 0x%08x: got 0x%08x expected 0x%08x\n",name,seed,actual,expected);
        ++mismatches;
    }
}
static void normal(tic80 *tic,const char *mode,unsigned frame)
{
    const uint32_t expected[]={241,15,15,3,1,13,0,0,0,1};
    for(unsigned slot=0;slot<sizeof expected/sizeof *expected;++slot) {
        uint32_t actual=tic_api_pmem((tic_mem *)tic,slot,0,false);
        if(actual!=expected[slot]) {
            fprintf(stderr,"WASM %s cartridge frame %u slot %u: got 0x%08x expected 0x%08x\n",mode,frame,slot,actual,expected[slot]);
            ++mismatches;
        }
    }
}
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1 || (argc==4 && !strcmp(argv[1],"--export-cart")));
    setvbuf(stdout,NULL,_IONBF,0);
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart);
    bool found=false;
    FOREACH_LANG(s) if(!strcmp(s->name,"wasm")) { cart->lang=s->id; found=true; }
    CHECK(found);
    if(argc==4) {
        CHECK(strlen(argv[3])<120 && strspn(argv[3],"abcdefghijklmnopqrstuvwxyz0123456789-")==strlen(argv[3]));
        snprintf(cart->code.data,sizeof cart->code.data,"-- script: wasm\n-- saveid: %s\n",argv[3]);
    } else strcpy(cart->code.data,"// script: wasm\n");
    memcpy(cart->binary.data,wasm_abi_case,sizeof wasm_abi_case);
    cart->binary.size=sizeof wasm_abi_case;
    memset(&cart->banks[0].tiles,0xf1,sizeof cart->banks[0].tiles);
    u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes);
    s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
    if(argc==4) {
        FILE *file=fopen(argv[2],"wb"); CHECK(file);
        CHECK(fwrite(bytes,1,size,file)==(size_t)size); CHECK(!fclose(file));
        free(bytes); free(cart); return 0;
    }
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(tic);
    tic->callback.error=error;
    tic80_load(tic,bytes,size);
    tm_vm *vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK);
    free(bytes); free(cart);
    tic80_tick(tic,(tic80_input){0},counter,frequency); tic80_sound(tic); CHECK(!errors);
    tic_core *core=(tic_core *)tic;
    normal(tic,"direct",0);
    for(unsigned frame=0;frame<4;++frame) {
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
        normal(tm_vm_product(vm),"supervised",frame);
    }
    tm_vm_close(vm);
    IM3Runtime runtime=core->currentVM; CHECK(runtime);
    IM3Function pmem=find(runtime,"pmem"),peek=find(runtime,"peek"),peek4=find(runtime,"peek4"),
        peek2=find(runtime,"peek2"),peek1=find(runtime,"peek1"),pix=find(runtime,"pix"),vbank=find(runtime,"vbank");
    const uint32_t seeds[]={0x7ead0080,0x00ffff00,0xffffff00,0x80000100};
    for(unsigned i=0;i<sizeof seeds/sizeof *seeds;++i) {
        uint32_t seed=seeds[i];
        core->api.poke((tic_mem *)tic,0x4000,0xf1,8);
        poison(pmem,seed); CHECK(!m3_CallV(peek,(int32_t)0x4000,(int32_t)8)); expect(peek,"peek8",241,seed);
        poison(pmem,seed); CHECK(!m3_CallV(peek,(int32_t)(0x4000*2+1),(int32_t)4)); expect(peek,"peek4 generic",15,seed);
        poison(pmem,seed); CHECK(!m3_CallV(peek4,(int32_t)(0x4000*2+1))); expect(peek4,"peek4",15,seed);
        poison(pmem,seed); CHECK(!m3_CallV(peek2,(int32_t)(0x4000*4+3))); expect(peek2,"peek2",3,seed);
        poison(pmem,seed); CHECK(!m3_CallV(peek1,(int32_t)(0x4000*8+7))); expect(peek1,"peek1",1,seed);
        core->api.vbank((tic_mem *)tic,0);
        core->api.pix((tic_mem *)tic,3,4,13,false);
        poison(pmem,seed); CHECK(!m3_CallV(pix,(int32_t)3,(int32_t)4,(int32_t)-1)); expect(pix,"pix get",13,seed);
        poison(pmem,seed); CHECK(!m3_CallV(pix,(int32_t)3,(int32_t)4,(int32_t)6)); expect(pix,"pix set",0,seed);
        CHECK(core->api.pix((tic_mem *)tic,3,4,0,true)==6);
        poison(pmem,seed); CHECK(!m3_CallV(vbank,(int32_t)-1)); expect(vbank,"vbank query zero",0,seed);
        poison(pmem,seed); CHECK(!m3_CallV(vbank,(int32_t)1)); expect(vbank,"vbank switch",0,seed);
        poison(pmem,seed); CHECK(!m3_CallV(vbank,(int32_t)-1)); expect(vbank,"vbank query one",1,seed);
    }
    tic80_delete(tic);
    CHECK(!errors);
    if(mismatches) { fprintf(stderr,"%u WASM return-slot mismatches\n",mismatches); return 1; }
    puts("WASM byte results: 10 direct and 40 supervised cartridge probes plus 40 seeded linked-import reads/writes passed");
    return 0;
}
