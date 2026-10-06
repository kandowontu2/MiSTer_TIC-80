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
#include "wasm_mouse_bounds_case.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned errors;
static void error(const char *text) { fprintf(stderr,"TIC-80: %s\n",text); ++errors; }
static u64 counter(void *data) { (void)data; return 0; }
static u64 frequency(void *data) { (void)data; return 60; }
static u8 *cart_bytes(const u8 *binary,size_t binary_size,size_t *size)
{
    tic_cartridge *cart=calloc(1,sizeof *cart);CHECK(cart);
    bool found=false;
    FOREACH_LANG(s) if(!strcmp(s->name,"wasm")) { cart->lang=s->id; found=true; }
    CHECK(found);strcpy(cart->code.data,"// script: wasm\n");
    CHECK(binary_size<=sizeof cart->binary.data);
    memcpy(cart->binary.data,binary,binary_size);cart->binary.size=binary_size;
    u8 *bytes=malloc(sizeof *cart*2);CHECK(bytes);
    s32 length=tic_cart_save(cart,bytes);CHECK(length>0);*size=(size_t)length;
    free(cart);return bytes;
}
static tic80 *create(void)
{
    size_t size;u8 *bytes=cart_bytes(wasm_mouse_bounds,sizeof wasm_mouse_bounds,&size);
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888);CHECK(tic);
    tic->callback.error=error;tic80_load(tic,bytes,size);free(bytes);
    tic80_tick(tic,(tic80_input){0},counter,frequency);CHECK(!errors);
    return tic;
}
static IM3Function mouse_function(tic80 *tic)
{
    IM3Function fun=NULL;
    CHECK(!m3_FindFunction(&fun,((tic_core *)tic)->currentVM,"mouse") && fun);
    return fun;
}
static void positive(tic80 *tic,unsigned variant,uint32_t offset)
{
    tic_core *core=(tic_core *)tic;tic_mem *mem=(tic_mem *)core;
    uint32_t size;u8 *linear=m3_GetMemory(core->currentVM,&size,0);CHECK(linear && size>=65536);
    tic80_mouse input={0};
    if(variant) { input.relative=true; input.rx=-128; input.ry=127; input.scrollx=31;input.scrolly=-32;input.left=true;input.right=true; }
    else { input.x=0;input.y=255;input.scrollx=-32;input.scrolly=31;input.middle=true; }
    mem->ram->input.mouse=input;
    /* Expected bytes follow the public WASM ABI, independently of the binding. */
    const u8 absolute[9]={0xf8,0xff,0xfb,0,0xe0,0x1f,0,1,0};
    const u8 relative[9]={0x80,0xff,0x7f,0,0x1f,0xe0,1,0,1};
    u8 *expected=malloc(size);CHECK(expected);memcpy(expected,linear,size);
    CHECK(offset<=size-9);memcpy(expected+offset,variant?relative:absolute,9);
    CHECK(!m3_CallV(mouse_function(tic),(int32_t)offset));
    CHECK(!memcmp(linear,expected,size));free(expected);
}
static void invalid(tic80 *tic,uint32_t offset)
{
    tic_core *core=(tic_core *)tic;uint32_t size;
    u8 *linear=m3_GetMemory(core->currentVM,&size,0),*before=malloc(size);CHECK(linear && before);
    memcpy(before,linear,size);
    M3Result result=m3_CallV(mouse_function(tic),(int32_t)offset);
    CHECK(result && !strcmp(result,m3Err_trapOutOfBoundsMemoryAccess));
    CHECK(!memcmp(before,linear,size));free(before);
}
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--vm-worker"))return tm_vm_worker(argc,argv);
    CHECK(argc==1 || (argc==2 && !strcmp(argv[1],"--pre-fix-probe")));
    setvbuf(stdout,NULL,_IONBF,0);
    tic80 *peers[2]={create(),create()};
    uint32_t size=m3_GetMemorySize(((tic_core *)peers[0])->currentVM);
    CHECK(size==m3_GetMemorySize(((tic_core *)peers[1])->currentVM));
    if(argc==2) { invalid(peers[0],size-8); return 0; }
    const uint32_t valid[]={0,1024,1025,size-10,size-9};
    for(unsigned pass=0;pass<2;++pass)for(unsigned p=0;p<2;++p)
        for(unsigned n=0;n<sizeof valid/sizeof *valid;++n)positive(peers[p],(pass+p)&1,valid[n]);
    const uint32_t bad[]={size-8,size-7,size-6,size-5,size-4,size-3,size-2,size-1,size,size+1,0xfffffff0,0xffffffff};
    for(unsigned p=0;p<2;++p)for(unsigned n=0;n<sizeof bad/sizeof *bad;++n)invalid(peers[p],bad[n]);
    /* A rejected destination leaves each independent runtime callable. */
    for(unsigned p=0;p<2;++p) { positive(peers[p],p,1025);tic80_delete(peers[p]); }
    size_t good_size,bad_size;
    u8 *good=cart_bytes(wasm_mouse_valid,sizeof wasm_mouse_valid,&good_size);
    u8 *bad_cart=cart_bytes(wasm_mouse_invalid,sizeof wasm_mouse_invalid,&bad_size);
    tm_vm *vm=NULL;CHECK(tm_vm_open(&vm,bad_cart,bad_size)==TM_VM_OK);
    CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_ERROR);
    CHECK(tic_api_pmem((tic_mem *)tm_vm_product(vm),0,0,false)==0);tm_vm_close(vm);
    CHECK(tm_vm_open(&vm,good,good_size)==TM_VM_OK);
    CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
    CHECK(tic_api_pmem((tic_mem *)tm_vm_product(vm),0,0,false)==123);tm_vm_close(vm);
    free(good);free(bad_cart);CHECK(!errors);
    puts("WASM mouse bounds: 22 valid/alignment calls, 24 traps with unchanged memory, supervised fault and replacement passed");
    return 0;
}
