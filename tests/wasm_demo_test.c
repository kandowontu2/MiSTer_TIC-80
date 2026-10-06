/* Run the standard cartridge against independently saved full-frame references. */
#include "tic80.h"
#include "tic.h"
#include "core/core.h"
#include "wasm3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"WASM demo failed line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned errors;
static u64 tick;
static void error(const char *text){fprintf(stderr,"TIC-80: %s\n",text);++errors;}
static void trace(const char *text,u8 color){(void)text;(void)color;}
static u64 counter(void *data){(void)data;return tick;}
static u64 frequency(void *data){(void)data;return 60;}
static void compare(tic80 *tic,const char *path)
{
    size_t size=TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4;
    unsigned char *expected=malloc(size);CHECK(expected);
    FILE *f=fopen(path,"rb");CHECK(f);
    CHECK(fread(expected,1,size,f)==size && fgetc(f)==EOF);CHECK(!fclose(f));
    CHECK(!memcmp(expected,tic->screen,size));free(expected);
}
int main(int argc,char **argv)
{
    CHECK(argc==4);FILE *f=fopen(argv[1],"rb");CHECK(f);
    CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>4 && size<4*1024*1024);rewind(f);
    u8 *bytes=malloc((size_t)size);CHECK(bytes);CHECK(fread(bytes,1,(size_t)size,f)==(size_t)size);CHECK(!fclose(f));
    tic80 *tic=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888);CHECK(tic);
    tic->callback.error=error;tic->callback.trace=trace;tic80_load(tic,bytes,(s32)size);free(bytes);
    void *ram=NULL;
    for(tick=0;tick<60;++tick) {
        tic80_tick(tic,(tic80_input){0},counter,frequency);tic80_sound(tic);CHECK(!errors);
        IM3Runtime runtime=((tic_core *)tic)->currentVM;CHECK(runtime);
        CHECK(m3_GetMemorySize(runtime)==2*65536);
        if(!tick)ram=m3_GetMemory(runtime,NULL,0);
        CHECK(ram && ((tic_mem *)tic)->ram==ram && m3_GetMemory(runtime,NULL,0)==ram);
        if(tick==29)compare(tic,argv[2]);
        if(tick==59)compare(tic,argv[3]);
    }
    tic80_delete(tic);puts("Standard WASM demo: two-page import, stable RAM and exact 30/60-tick RGBA frames passed");
    return 0;
}
