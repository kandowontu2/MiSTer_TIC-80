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
#include "wasm_pointer_case.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"pointer failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned errors,traces;
static void error(const char *text) { fprintf(stderr,"TIC-80: %s\n",text);++errors; }
/* Never read the supplied pointer: the pre-fix probe is contained in this process. */
static void trace(const char *text,u8 color) { (void)text;(void)color;++traces; }
static u64 counter(void *data) { (void)data;return 0; }
static u64 frequency(void *data) { (void)data;return 60; }
static u8 *cart_bytes(const u8 *binary,size_t binary_size,size_t *length)
{
    tic_cartridge *cart=calloc(1,sizeof *cart);CHECK(cart);bool found=false;
    FOREACH_LANG(s) {
        if(!strcmp(s->name,"wasm")) { cart->lang=s->id;found=true; }
    }
    CHECK(found);
    strcpy(cart->code.data,"// script: wasm\n");CHECK(binary_size<=sizeof cart->binary.data);
    memcpy(cart->binary.data,binary,binary_size);cart->binary.size=binary_size;
    u8 *bytes=malloc(sizeof *cart*2);CHECK(bytes);s32 size=tic_cart_save(cart,bytes);CHECK(size>0);
    *length=(size_t)size;free(cart);return bytes;
}
static tic80 *create(void)
{
    size_t size;u8 *bytes=cart_bytes(wasm_pointer_case,sizeof wasm_pointer_case,&size);
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888);CHECK(tic);tic->callback.error=error;tic->callback.trace=trace;
    tic80_load(tic,bytes,size);free(bytes);tic80_tick(tic,(tic80_input){0},counter,frequency);CHECK(!errors);return tic;
}
static IM3Function find(tic80 *tic,const char *name)
{
    IM3Function f=NULL;CHECK(!m3_FindFunction(&f,((tic_core *)tic)->currentVM,name) && f);return f;
}
static M3Result call(tic80 *tic,const char *name,uint32_t text,uint32_t colors,int count,uint32_t remap)
{
    IM3Function f=find(tic,name);
    if(!strcmp(name,"trace"))return m3_CallV(f,(int32_t)text,(int32_t)15);
    if(!strcmp(name,"print"))return m3_CallV(f,(int32_t)text,20,20,15,1,1,0);
    if(!strcmp(name,"font"))return m3_CallV(f,(int32_t)text,20,20,(int32_t)colors,count,8,8,1,1,0);
    if(!strcmp(name,"spr"))return m3_CallV(f,0,20,20,(int32_t)colors,count,1,0,0,1,1);
    if(!strcmp(name,"map"))return m3_CallV(f,0,0,1,1,20,20,(int32_t)colors,count,1,(int32_t)remap);
    CHECK(!strcmp(name,"ttri"));
    return m3_CallV(f,20.,20.,28.,20.,20.,28.,0.,0.,8.,0.,0.,8.,0,(int32_t)colors,count,0.,0.,0.,0);
}
static void reject(tic80 *tic,const char *name,uint32_t text,uint32_t colors,int count,uint32_t remap,bool bounds)
{
    uint32_t size;u8 *linear=m3_GetMemory(((tic_core *)tic)->currentVM,&size,0),*before=malloc(size);CHECK(before);
    memcpy(before,linear,size);unsigned old_traces=traces;
    M3Result result=call(tic,name,text,colors,count,remap);
    CHECK(result && (!bounds || !strcmp(result,m3Err_trapOutOfBoundsMemoryAccess)));
    CHECK(!memcmp(before,linear,size));CHECK(traces==old_traces);free(before);
}
static void write32(u8 *where,uint32_t word)
{
    for(unsigned n=0;n<4;++n)where[n]=(u8)(word>>(n*8));
}
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--vm-worker"))return tm_vm_worker(argc,argv);
    CHECK(argc==1 || (argc==2 && !strcmp(argv[1],"--pre-fix-probe")));setvbuf(stdout,NULL,_IONBF,0);
    tic80 *tic=create();uint32_t size;u8 *linear=m3_GetMemory(((tic_core *)tic)->currentVM,&size,0);CHECK(size>65536);
    if(argc==2){reject(tic,"trace",0xffffffff,0,0,0,true);return 0;}
    const char *strings[]={"trace","print","font"};
    for(unsigned n=0;n<3;++n){
        linear[size-1]='X';reject(tic,strings[n],size-1,0,0,0,true);
        reject(tic,strings[n],size,0,0,0,true);reject(tic,strings[n],0xffffffff,0,0,0,true);
        linear[size-1]=0;CHECK(!call(tic,strings[n],size-1,0,0,0));
        linear[size-2]='A';CHECK(!call(tic,strings[n],size-2,0,0,0));
    }
    memcpy(linear+1024,"safe",5);
    const char *arrays[]={"spr","map","ttri","font"};
    for(unsigned n=0;n<4;++n){
        reject(tic,arrays[n],1024,size-1,2,0,true);
        reject(tic,arrays[n],1024,size,1,0,true);
        reject(tic,arrays[n],1024,0xffffffff,1,0,true);
        linear[size-1]=0;CHECK(!call(tic,arrays[n],1024,size-1,1,0));
        CHECK(!call(tic,arrays[n],1024,0xffffffff,0,0));
        CHECK(!call(tic,arrays[n],1024,0,1,0));
    }
    /* Optional colors pointer zero must behave as C NULL, never as VRAM offset zero. */
    tic_mem *mem=(tic_mem *)tic;tic_core *core=(tic_core *)tic;
    core->api.cls(mem,5);mem->ram->vram.screen.data[0]=0;memset(&mem->ram->tiles,0,sizeof mem->ram->tiles);
    CHECK(!call(tic,"spr",0,0,1,0));CHECK(core->api.pix(mem,20,20,0,true)==0);
    /* Both descriptor and result may be unaligned; all raw result bytes are independent expectations. */
    const uint32_t descriptor=2049,destination=size-13;
    write32(linear+descriptor,0);write32(linear+descriptor+4,77);write32(linear+descriptor+8,destination);
    reject(tic,"map",0,0,0,size-11,true);reject(tic,"map",0,0,0,0xffffffff,true);
    write32(linear+descriptor+8,size-11);reject(tic,"map",0,0,0,descriptor,true);
    write32(linear+descriptor+8,destination);
    /* Wrong types at the correct arity, wrong arity, non-void return, empty
     * table slot and out-of-range index must all stop before map writes. */
    for(unsigned table=1;table<=5;++table){write32(linear+descriptor,table);reject(tic,"map",0,0,0,descriptor,false);}
    write32(linear+descriptor,0);memset(linear+destination,0x5a,12);
    memset(&mem->ram->tiles.data[9],0xdd,sizeof mem->ram->tiles.data[9]);
    CHECK(!call(tic,"map",0,0,0,descriptor));
    const u8 expected[12]={9,0,0,0,1,0,0,0,2,0,0,0};CHECK(!memcmp(linear+destination,expected,12));
    CHECK(core->api.pix(mem,20,20,0,true)==13);
    CHECK(!errors && traces==2);tic80_delete(tic);
    for(unsigned n=0;n<sizeof pointer_fault_cases/sizeof *pointer_fault_cases;++n) {
        size_t bad_size,good_size;u8 *bad=cart_bytes(pointer_fault_cases[n].bytes,pointer_fault_cases[n].size,&bad_size);
        u8 *good=cart_bytes(wasm_pointer_good,sizeof wasm_pointer_good,&good_size);
        tm_vm *vm=NULL;CHECK(tm_vm_open(&vm,bad,bad_size)==TM_VM_OK);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_ERROR);tm_vm_close(vm);
        CHECK(tm_vm_open(&vm,good,good_size)==TM_VM_OK);CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);tm_vm_close(vm);
        free(bad);free(good);printf("%s: supervised pointer fault and replacement passed\n",pointer_fault_cases[n].name);
    }
    puts("WASM pointers: bounded strings/colors, C NULL colors, unaligned remap, invalid callbacks, unchanged rejected memory and six supervised faults passed");return 0;
}
