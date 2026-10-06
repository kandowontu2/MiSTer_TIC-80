#include "tic80.h"
#include "tic.h"
#include "cart.h"
#include "script.h"
#include "core/core.h"
#include "wasm3.h"
#include "m3_env.h"
#include "tic80_mister/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm_memory_case.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"memory failed line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned errors,moves;
static void *linear_allocation;
static bool fail_allocation;
void *__real_m3_Realloc_Impl(void *,size_t,size_t);
void *__wrap_m3_Realloc_Impl(void *ptr,size_t next_size,size_t old_size)
{
    if(fail_allocation && !ptr && next_size==4*65536+sizeof(M3MemoryHeader))return NULL;
    if(ptr && ptr==linear_allocation && next_size!=old_size) {
        void *next=malloc(next_size);if(!next)return NULL;
        memcpy(next,ptr,next_size<old_size?next_size:old_size);
        if(next_size>old_size)memset((u8 *)next+old_size,0,next_size-old_size);
        free(ptr);++moves;linear_allocation=next;return next;
    }
    return __real_m3_Realloc_Impl(ptr,next_size,old_size);
}
static void error(const char *text){fprintf(stderr,"TIC-80: %s\n",text);++errors;}
static void trace(const char *text,u8 color){(void)text;(void)color;}
static u64 counter(void *p){(void)p;return 0;}
static u64 frequency(void *p){(void)p;return 60;}
static u8 *cart_bytes(const u8 *module,size_t length,size_t *size)
{
    tic_cartridge *cart=calloc(1,sizeof *cart);CHECK(cart);bool found=false;
    FOREACH_LANG(s) {if(!strcmp(s->name,"wasm")){cart->lang=s->id;found=true;}}
    CHECK(found);strcpy(cart->code.data,"// script: wasm\n");
    memcpy(cart->binary.data,module,length);cart->binary.size=length;
    u8 *bytes=malloc(sizeof *cart*2);CHECK(bytes);s32 written=tic_cart_save(cart,bytes);CHECK(written>0);
    *size=written;free(cart);return bytes;
}
static tic80 *create(const u8 *module,size_t length)
{
    size_t size;u8 *bytes=cart_bytes(module,length,&size);
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888);CHECK(tic);
    tic->callback.error=error;tic->callback.trace=trace;tic80_load(tic,bytes,size);free(bytes);
    tic80_tick(tic,(tic80_input){0},counter,frequency);return tic;
}
static uint32_t grow(IM3Runtime runtime,uint32_t count)
{
    IM3Function f=NULL;CHECK(!m3_FindFunction(&f,runtime,"grow"));CHECK(!m3_CallV(f,count));
    uint32_t previous=0;CHECK(!m3_GetResultsV(f,&previous));return previous;
}
static void write32(u8 *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=n>>(8*i);}
static void remap_grow(tic80 *tic,u8 *base,unsigned *pages,unsigned maximum)
{
    tic_mem *mem=(tic_mem *)tic;tic_core *core=(tic_core *)tic;IM3Runtime runtime=core->currentVM;
    const uint32_t descriptor=2049,destination=*pages*65536-13;
    write32(base+descriptor,0);write32(base+descriptor+4,1);write32(base+descriptor+8,destination);
    memset(&mem->ram->tiles.data[9],0xdd,sizeof mem->ram->tiles.data[9]);
    if(*pages<maximum)memset(base+*pages*65536,0x77,65536);
    base[*pages*65536-1]=0; /* Also retain a checked color-array pointer across the callback. */
    IM3Function map=NULL;CHECK(!m3_FindFunction(&map,runtime,"map"));
    CHECK(!m3_CallV(map,0,0,1,1,20,20,*pages*65536-1,1,1,descriptor));
    uint32_t previous=0;for(unsigned byte=0;byte<4;++byte)previous|=(uint32_t)base[1024+byte]<<(8*byte);
    if(*pages<maximum) {
        CHECK(previous==*pages);++*pages;
        for(unsigned n=(*pages-1)*65536;n<*pages*65536;++n)CHECK(base[n]==0);
    } else CHECK(previous==UINT32_MAX);
    const u8 expected[12]={9,0,0,0,1,0,0,0,2,0,0,0};
    CHECK(!memcmp(base+destination,expected,12));
    CHECK(core->api.pix(mem,20,20,0,true)==13 && !errors && !moves);
    CHECK(mem->ram==(void *)m3_GetMemory(runtime,NULL,0) && mem->ram==(void *)base);
    CHECK(m3_GetMemorySize(runtime)==*pages*65536);
}
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--vm-worker"))return tm_vm_worker(argc,argv);
    CHECK(argc==1);setvbuf(stdout,NULL,_IONBF,0);
    for(unsigned i=0;i<sizeof memory_cases/sizeof *memory_cases;++i) {
        errors=0;moves=0;linear_allocation=NULL;
        tic80 *tic=create(memory_cases[i].bytes,memory_cases[i].size);
        if(!memory_cases[i].valid) {
            CHECK(errors>0);tic80_delete(tic);printf("%s: incompatible memory rejected\n",memory_cases[i].name);continue;
        }
        CHECK(!errors);tic_mem *mem=(tic_mem *)tic;tic_core *core=(tic_core *)tic;IM3Runtime runtime=core->currentVM;
        u8 *base=m3_GetMemory(runtime,NULL,0);linear_allocation=runtime->memory.mallocated;
        CHECK(mem->ram==(void *)base && m3_GetMemorySize(runtime)==memory_cases[i].pages*65536);
        unsigned pages=memory_cases[i].pages,maximum=memory_cases[i].maximum;
        CHECK(grow(runtime,0)==pages);
        remap_grow(tic,base,&pages,maximum);
        while(pages<maximum) {
            /* Poison physically reserved, logically inaccessible bytes. Growth must zero them. */
            memset(base+pages*65536,0x77,65536);base[pages*65536-1]=0xa5;
            CHECK(grow(runtime,1)==pages);++pages;
            CHECK(mem->ram==(void *)m3_GetMemory(runtime,NULL,0) && mem->ram==(void *)base && !moves);
            CHECK(base[(pages-1)*65536-1]==0xa5);
            for(unsigned n=(pages-1)*65536;n<pages*65536;++n)CHECK(base[n]==0);
        }
        CHECK(grow(runtime,1)==UINT32_MAX && grow(runtime,UINT32_MAX)==UINT32_MAX);
        CHECK(grow(runtime,UINT32_MAX-1)==UINT32_MAX && m3_GetMemorySize(runtime)==pages*65536);
        remap_grow(tic,base,&pages,maximum);
        tic80_tick(tic,(tic80_input){0},counter,frequency);CHECK(!errors && mem->ram==(void *)base);
        tic80_delete(tic);printf("%s: bounded growth, stable RAM, zero pages and remap growth rejection passed\n",memory_cases[i].name);
    }
    errors=0;fail_allocation=true;
    tic80 *failed=create(memory_defined4,sizeof memory_defined4);CHECK(errors>0);tic80_delete(failed);fail_allocation=false;
    errors=0;tic80 *good=create(memory_defined4,sizeof memory_defined4);CHECK(!errors);tic80_delete(good);
    for(unsigned i=0;i<sizeof memory_cases/sizeof *memory_cases;++i)if(!memory_cases[i].valid) {
        size_t bad_size,good_size;u8 *bad=cart_bytes(memory_cases[i].bytes,memory_cases[i].size,&bad_size);
        u8 *replacement=cart_bytes(memory_defined4,sizeof memory_defined4,&good_size);
        tm_vm *vm=NULL;CHECK(tm_vm_open(&vm,bad,bad_size)==TM_VM_OK);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_ERROR);tm_vm_close(vm);
        CHECK(tm_vm_open(&vm,replacement,good_size)==TM_VM_OK);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);tm_vm_close(vm);
        free(bad);free(replacement);printf("%s: supervised memory rejection and replacement passed\n",memory_cases[i].name);
    }
    puts("WASM memory: 20 defined/imported limits, stable backing, zero-fill, overflow, remap, allocation recovery and five supervised replacements passed");
    return 0;
}
