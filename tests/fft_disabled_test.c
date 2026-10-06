#include "tic80_mister/vm.h"
#include "cart.h"
#include "tic.h"
#include "api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv)
{
    if(argc>=2&&!strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    assert(argc==1&&!tm_fft_supported());
    tic_cartridge* cart=calloc(1,sizeof *cart); assert(cart);
    strcpy(cart->code.data,"-- script: lua\nfunction TIC() pmem(0,123) pmem(1,fft(32)+ffts(32)+fftr(32)+fftrs(32)) end\n");
    u8* bytes=malloc(sizeof *cart*2); assert(bytes); s32 size=tic_cart_save(cart,bytes); assert(size>0); free(cart);
    tm_fft_config config={0}; assert(!tm_fft_configure(&config,"Mic A"));
    tm_vm* vm=NULL; assert(tm_vm_open_configured(&vm,bytes,size,&config)==TM_VM_OK);
    assert(tm_vm_fft_status(vm)==TM_FFT_UNAVAILABLE);
    assert(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
    assert(((tic_mem*)tm_vm_product(vm))->ram->persistent.data[0]==123);
    assert(!((tic_mem*)tm_vm_product(vm))->ram->persistent.data[1]);
    tm_vm_close(vm); free(bytes);
    puts("Capture-disabled build reports unavailable, preserves cartridge execution and returns zero FFT"); return 0;
}
