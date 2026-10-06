#include "tic80.h"
#include "tic.h"
#include "cart.h"
#include "api.h"
#include "script.h"
#include "tools.h"
#include "tic80_mister/cart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static int errors;
static u64 clock_ticks;
static const char *export_directory;
static void error(const char *message) { ++errors; fprintf(stderr,"runtime: %s\n",message); }
static u64 counter(void *data) { (void)data; return clock_ticks; }
static u64 frequency(void *data) { (void)data; return 60; }
static void tick(tic80 *tic,unsigned buttons)
{
    tic80_input input={0}; input.gamepads.data=buttons;
    tic80_tick(tic,input,counter,frequency); tic80_sound(tic);
    CHECK(tic->samples.count==1600);
}
static tic80 *load(const u8 *bytes,s32 size)
{
    CHECK(!tm_cart_validate(bytes,(size_t)size));
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888);
    CHECK(tic); tic->callback.error=error; tic80_load(tic,(void *)bytes,size);
    return tic;
}
static const tic_script *script(const char *name)
{
    FOREACH_LANG(s) if(!strcmp(s->name,name)) return s;
    fprintf(stderr,"missing runtime %s\n",name); exit(1);
}
static void demos(void)
{
    static const char *names[]={"lua","js","moon","yue","fennel","scheme","squirrel","python","wren","janet","wasm","ruby","miniscript","forth"};
    u8 *bytes=malloc(sizeof(tic_cartridge)); CHECK(bytes);
    for(unsigned n=0;n<sizeof names/sizeof *names;++n) {
        const tic_script *s=script(names[n]);
        s32 size=tic_tool_unzip(bytes,sizeof(tic_cartridge),s->demo.data,s->demo.size);
        CHECK(size>0);
        if(export_directory) {
            char path[1024];
            int length=snprintf(path,sizeof path,"%s/%s.tic",export_directory,names[n]);
            CHECK(length>0 && (size_t)length<sizeof path);
            FILE *file=fopen(path,"wb"); CHECK(file);
            CHECK(fwrite(bytes,1,(size_t)size,file)==(size_t)size); CHECK(!fclose(file));
        }
        tic80 *a=load(bytes,size),*b=load(bytes,size);
        CHECK(!strcmp(tic_get_script((tic_mem *)a)->name,names[n]));
        errors=0;
        for(clock_ticks=0;clock_ticks<120;++clock_ticks) {
            tick(a,0); tick(b,0); CHECK(!errors);
            CHECK(!memcmp(a->screen,b->screen,TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4));
        }
        unsigned colors=0; u32 seen[256];
        for(unsigned p=0;p<TIC80_FULLWIDTH*TIC80_FULLHEIGHT;++p) {
            unsigned i=0; while(i<colors && seen[i]!=a->screen[p]) ++i;
            if(i==colors && colors<256) seen[colors++]=a->screen[p];
        }
        CHECK(colors>=3);
        tick(a,8); tick(b,0); CHECK(!errors);
        CHECK(memcmp(a->screen,b->screen,TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4));
        tic80_delete(a);
        for(unsigned i=0;i<60;++i) { ++clock_ticks; tick(b,0); CHECK(!errors); }
        tic80_delete(b);
        printf("%s: native demo, 120 interleaved ticks, input and peer deletion passed\n",names[n]);
    }
    free(bytes);
}
static void isolation(const char *name,const char *code)
{
    const tic_script *s=script(name);
    tic_cartridge *cart=calloc(1,sizeof *cart); CHECK(cart);
    cart->lang=s->id;
    strcpy(cart->code.data,code);
    u8 *bytes=malloc(sizeof *cart*2); CHECK(bytes);
    s32 size=tic_cart_save(cart,bytes);
    tic80 *old=load(bytes,size),*next=load(bytes,size);
    CHECK(((tic_mem *)old)->cart.lang==s->id);
    CHECK(!strcmp(tic_get_script((tic_mem *)old)->name,name));
    errors=0;
    for(unsigned i=0;i<45;++i) { ++clock_ticks; tick(old,0); CHECK(!errors); }
    for(unsigned i=0;i<3;++i) { ++clock_ticks; tick(next,0); CHECK(!errors); }
    CHECK(tic_api_pmem((tic_mem *)old,0,0,false)==45);
    CHECK(tic_api_pmem((tic_mem *)next,0,0,false)==3);
    if(!strcmp(name,"forth")) {
        CHECK(tic_api_pmem((tic_mem *)old,2,0,false)==99);
        CHECK(tic_api_pmem((tic_mem *)next,2,0,false)==99);
    }
    tic80_delete(old);
    tick(next,0); CHECK(!errors);
    CHECK(tic_api_pmem((tic_mem *)next,0,0,false)==4);
    strcpy(cart->code.data,"@] invalid source !!!");
    size=tic_cart_save(cart,bytes);
    tic80 *bad=load(bytes,size);
    CHECK(!strcmp(tic_get_script((tic_mem *)bad)->name,name));
    errors=0; tick(bad,0); CHECK(errors);
    tic80_delete(bad); errors=0; tick(next,0); CHECK(!errors);
    CHECK(tic_api_pmem((tic_mem *)next,0,0,false)==5);
    tic80_delete(next); free(bytes); free(cart);
    printf("%s: independent state, hot-swap and failed-candidate recovery passed\n",name);
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--export")) export_directory=argv[2];
    else CHECK(argc==1);
    setvbuf(stdout,NULL,_IONBF,0);
    demos();
    isolation("python",
        "# script: python\nimport easing\n"
        "for name in ['Linear','InSine','OutSine','InOutSine','InQuad','OutQuad','InOutQuad',"
        "'InCubic','OutCubic','InOutCubic','InQuart','OutQuart','InOutQuart',"
        "'InQuint','OutQuint','InOutQuint','InExpo','OutExpo','InOutExpo',"
        "'InCirc','OutCirc','InOutCirc','InBack','OutBack','InOutBack',"
        "'InElastic','OutElastic','InOutElastic','InBounce','OutBounce','InOutBounce']:\n"
        " base=getattr(easing,name)\n alias=getattr(easing,'Ease'+name)\n"
        " assert base is alias\n assert alias(0.5)==base(0.5)\n"
        "n=0\ndef TIC():\n global n\n n+=1\n pmem(0,n)\n");
    isolation("janet","# script: janet\n(import tic80)\n(var n 0)\n(defn TIC [] (++ n) (tic80/pmem 0 n))\n");
    isolation("wren","// script: wren\nclass Game is TIC {\n construct new() { _n=0 }\n TIC() {\n _n=_n+1\n TIC.pmem(0,_n)\n }\n}\n");
    isolation("forth","\\ script: forth\nVARIABLE N\n0 N !\n: TIC N @ 1+ DUP N ! 0 PMEM!\n N @ 3 = IF S\" : SCN DROP 99 2 PMEM! ;\" EVALUATE THEN ;\n");
    isolation("ruby","# script: ruby\n$n=0\ndef TIC\n $n+=1\n pmem(0,$n)\nend\n");
    return 0;
}
