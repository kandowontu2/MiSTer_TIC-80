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
#include "wasm_memory_declaration_cases.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"declaration failed line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned errors;
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
int main(int argc,char **argv)
{
    if(argc>=2 && !strcmp(argv[1],"--vm-worker"))return tm_vm_worker(argc,argv);
    CHECK(argc==1);setvbuf(stdout,NULL,_IONBF,0);
    size_t replacement_size;u8 *replacement=cart_bytes(declaration_import_fixed_four,sizeof declaration_import_fixed_four,&replacement_size);
    unsigned valid=0,rejected=0;
    for(unsigned i=0;i<sizeof declaration_cases/sizeof *declaration_cases;++i) {
        size_t size;u8 *bytes=cart_bytes(declaration_cases[i].bytes,declaration_cases[i].size,&size);
        errors=0;tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888);CHECK(tic);
        tic->callback.error=error;tic->callback.trace=trace;tic80_load(tic,bytes,size);
        tic80_tick(tic,(tic80_input){0},counter,frequency);
        if(declaration_cases[i].valid) {
            CHECK(!errors);IM3Runtime runtime=((tic_core *)tic)->currentVM;CHECK(runtime);
            CHECK(m3_GetMemorySize(runtime)==declaration_cases[i].pages*65536);
            CHECK(((tic_mem *)tic)->ram==(void *)m3_GetMemory(runtime,NULL,0));++valid;
        } else {CHECK(errors>0);++rejected;}
        tic80_delete(tic);
        tm_vm *vm=NULL;CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==(declaration_cases[i].valid?TM_VM_OK:TM_VM_ERROR));tm_vm_close(vm);
        CHECK(tm_vm_open(&vm,replacement,replacement_size)==TM_VM_OK);
        CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);tm_vm_close(vm);
        free(bytes);printf("%s: declaration contract and supervised replacement passed\n",declaration_cases[i].name);
    }
    free(replacement);CHECK(valid==8 && rejected==13);
    puts("WASM declarations: eight supported types, thirteen rejected limits/encodings and twenty-one supervised replacements passed");
    return 0;
}
