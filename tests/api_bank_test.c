#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/vm.h"
#include "tic80.h"
#include "tic.h"
#include "api.h"
#include "cart.h"
#include "script.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include "api_bank_cases.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
_Static_assert(offsetof(tic_ram,flags)==0x14404,"Fixture flags address must match the pinned RAM ABI");
static const tic_script *language(const char *name)
{
    FOREACH_LANG(s) if (!strcmp(s->name,name)) return s;
    CHECK(0); return NULL;
}
static void rgb(tic80 *tic,unsigned x,unsigned y,tic_rgb expected)
{
    const u8 *p=(const u8 *)tic->screen + ((y+TIC80_MARGIN_TOP)*TIC80_FULLWIDTH+x+TIC80_MARGIN_LEFT)*4;
    if (p[0]!=expected.r || p[1]!=expected.g || p[2]!=expected.b)
        fprintf(stderr,"pixel (%u,%u): RGB %u,%u,%u expected %u,%u,%u\n",x,y,p[0],p[1],p[2],expected.r,expected.g,expected.b);
    CHECK(p[0]==expected.r && p[1]==expected.g && p[2]==expected.b);
}
static unsigned direct_errors;
static u64 direct_ticks;
static void direct_error(const char *text) { fprintf(stderr,"direct: %s\n",text); ++direct_errors; }
static u64 counter(void *data) { (void)data; return direct_ticks; }
static u64 frequency(void *data) { (void)data; return 60; }
static void check_overline(tic80 *tic,unsigned variant,unsigned frame)
{
    CHECK(tic_api_pmem((tic_mem *)tic,0,0,false)==((variant==1 || variant==2)?frame+1:0));
    CHECK(tic_api_pmem((tic_mem *)tic,1,0,false)==0);
    rgb(tic,0,0,(tic_rgb){41,71,99});
    rgb(tic,10,10,variant==0?(tic_rgb){81,123,201}:(tic_rgb){0,0,0});
    rgb(tic,11,11,(variant==1 || variant==2)?(tic_rgb){101,151,211}:(tic_rgb){0,0,0});
}
static void wren_overlines(void)
{
    const char *tick=" TIC() {\n TIC.pmem(1,TIC.vbank(-1))\n TIC.vbank(1)\n TIC.cls(0)\n TIC.pix(10,10,2)\n TIC.vbank(0)\n TIC.cls(0)\n TIC.pix(0,0,1)\n }\n";
    const char *overline=" OVR() {\n TIC.pmem(0,TIC.pmem(0)+1)\n TIC.pix(11,11,3)\n }\n";
    tm_vm *vms[4]={0};
    tic80 *direct[4]={0};
    for(unsigned variant=0;variant<4;++variant) {
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart);
        cart->lang=language("wren")->id;
        if(variant==2)
            snprintf(cart->code.data,sizeof cart->code.data,
                "class Parent is TIC {\n construct new() {}\n%s}\nclass Game is Parent {\n construct new() { super() }\n%s}\n",overline,tick);
        else
            snprintf(cart->code.data,sizeof cart->code.data,
                "// OVR() in a comment is not an override\nclass Game is TIC {\n construct new() {\n var text=\"OVR()\"\n }\n%s%s}\n",
                tick,variant==1?overline:variant==3?" OVR() {}\n":"");
        cart->banks[0].palette.vbank0.colors[1]=(tic_rgb){41,71,99};
        cart->banks[0].palette.vbank1.colors[2]=(tic_rgb){81,123,201};
        cart->banks[0].palette.vbank1.colors[3]=(tic_rgb){101,151,211};
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes);
        s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        CHECK(tm_vm_open(&vms[variant],bytes,size)==TM_VM_OK);
        direct[variant]=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct[variant]);
        direct[variant]->callback.error=direct_error;
        tic80_load(direct[variant],bytes,size);
        free(bytes); free(cart);
    }
    for(unsigned frame=0;frame<3;++frame) for(unsigned variant=0;variant<4;++variant) {
        fprintf(stdout,"Wren overline variant %u frame %u\n",variant,frame);
        CHECK(tm_vm_tick(vms[variant],(tic80_input){0},0)==TM_VM_OK);
        check_overline(tm_vm_product(vms[variant]),variant,frame);
        direct_ticks=frame;
        tic80_tick(direct[variant],(tic80_input){0},counter,frequency);
        tic80_sound(direct[variant]); CHECK(!direct_errors);
        check_overline(direct[variant],variant,frame);
    }
    for(unsigned variant=0;variant<4;++variant) { tm_vm_close(vms[variant]); tic80_delete(direct[variant]); }
    puts("Wren default, overridden, inherited and explicit empty overlines passed in direct and supervised interleaved VMs");
}
int main(int argc,char **argv)
{
    if (argc>=2 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    CHECK(argc==1); setvbuf(stdout,NULL,_IONBF,0);
    wren_overlines();
    for (unsigned c=0;c<sizeof api_bank_cases/sizeof *api_bank_cases;++c) {
        const char *name=api_bank_cases[c].language;
        unsigned bank=api_bank_cases[c].bank;
        tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart);
        cart->lang=language(name)->id;
        strcpy(cart->code.data,api_bank_cases[c].source);
        if (api_bank_cases[c].binary) {
            CHECK(api_bank_cases[c].binary_size<sizeof cart->binary.data);
            memcpy(cart->binary.data,api_bank_cases[c].binary,api_bank_cases[c].binary_size);
            cart->binary.size=api_bank_cases[c].binary_size;
        }
        for (unsigned b=0;b<TIC_BANKS;++b) {
            memset(&cart->banks[b].tiles,(b+1)*17,sizeof cart->banks[b].tiles);
            memset(&cart->banks[b].sprites,(b+2)*17,sizeof cart->banks[b].sprites);
            cart->banks[b].map.data[0]=(u8)(17+b);
            cart->banks[b].flags.data[0]=(u8)(33+b);
            cart->banks[b].palette.vbank0.colors[1]=(tic_rgb){(u8)(40+b),71,99};
            cart->banks[b].palette.vbank1.colors[2]=(tic_rgb){(u8)(80+b),123,201};
        }
        u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes);
        s32 size=tic_cart_save(cart,bytes); CHECK(size>0);
        tm_vm *vm=NULL;
        int opened=tm_vm_open(&vm,bytes,size);
        if (opened!=TM_VM_OK) fprintf(stderr,"%s bank %u: open returned %d\n",name,bank,opened);
        CHECK(opened==TM_VM_OK);
        free(bytes); free(cart);
        for (unsigned frame=0;frame<4;++frame) {
            int result=tm_vm_tick(vm,(tic80_input){0},0);
            if (result!=TM_VM_OK) fprintf(stderr,"%s bank %u frame %u: result %d\n",name,bank,frame,result);
            CHECK(result==TM_VM_OK);
            unsigned selected=frame==3?(bank==7?0:7):bank;
            unsigned changed=frame==1 || frame==2;
            const u32 expected[]={changed?129+bank:(selected+1)*17,
                changed?145+bank:(selected+2)*17,changed?161+bank:17+selected,
                changed?177+bank:33+selected,40+selected,80+selected};
            tic80 *tic=tm_vm_product(vm);
            for (unsigned slot=0;slot<sizeof expected/sizeof *expected;++slot) {
                u32 got=tic_api_pmem((tic_mem *)tic,slot,0,false);
                if (got!=expected[slot]) fprintf(stderr,"%s bank %u frame %u slot %u: got %u expected %u\n",name,bank,frame,slot,got,expected[slot]);
                CHECK(got==expected[slot]);
            }
            CHECK(tic_api_pmem((tic_mem *)tic,15,0,false)==frame+1);
            fprintf(stdout,"%s bank %u frame %u: checking composited pixels\n",name,bank,frame);
            rgb(tic,0,0,(tic_rgb){(u8)(40+selected),71,99});
            rgb(tic,10,10,(tic_rgb){(u8)(80+selected),123,201});
            rgb(tic,20,20,(tic_rgb){0,0,0});
            CHECK(tic->samples.count==1600);
        }
        tm_vm_close(vm);
        printf("%s bank %u: asset read/write-back/isolation and two-bank palette composition passed\n",name,bank);
    }
    CHECK(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    puts("All 14 runtimes passed banks 0/1/7 without leaking a worker");
    return 0;
}
