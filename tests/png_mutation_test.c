/* Standalone cartridge decoder stress fixture. Run with ASan/UBSan and exported
 * png_cart_test fixtures; it deliberately avoids the interpreter processes. */
#include "tic80_mister/cart_file.h"
#include "tic80_mister/cart.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static uint32_t random_state=0x8d1325a7;
static uint32_t next(void) { random_state^=random_state<<13; random_state^=random_state>>17; return random_state^=random_state<<5; }
static uint32_t big32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p,uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static void repair_crc(uint8_t *bytes,size_t size)
{
    for (size_t at=8;at<=size && size-at>=12;) {
        uint32_t length=big32(bytes+at); if (length>size-at-12) return;
        put32(bytes+at+8+length,crc32(0,bytes+at+4,length+4)); at+=length+12;
    }
}
int main(int argc,char **argv)
{
    CHECK(argc>1); unsigned total=0,accepted=0;
    for (int arg=1;arg<argc;++arg) {
        FILE *file=fopen(argv[arg],"rb"); CHECK(file);
        CHECK(!fseek(file,0,SEEK_END)); long count=ftell(file); CHECK(count>0 && count<=4*1024*1024);
        CHECK(!fseek(file,0,SEEK_SET)); size_t size=count;
        uint8_t *seed=malloc(size),*mutated=malloc(size+16); CHECK(seed && mutated);
        CHECK(fread(seed,1,size,file)==size); CHECK(!fclose(file));
        for (unsigned trial=0;trial<2000;++trial) {
            size_t length=size; memcpy(mutated,seed,size);
            if (trial%5==0) length=next()%(size+1);
            else if (trial%5==1) { length+=next()%16; memset(mutated+size,0,length-size); }
            else {
                unsigned changes=1+next()%8;
                while (changes--) mutated[next()%size]^=1u<<(next()%8);
                if (trial%5>=3) repair_crc(mutated,length);
            }
            uint8_t *native=(void *)1; size_t native_size=123;
            int status=tm_cart_file_decode(mutated,length,&native,&native_size);
            if (!status) {
                CHECK(native && native_size<=4*1024*1024 && !tm_cart_validate(native,native_size)); ++accepted;
            } else CHECK(status==-1 && !native && !native_size);
            free(native); ++total;
        }
        free(seed); free(mutated);
    }
    printf("%u deterministic mutations completed; %u valid cartridges accepted\n",total,accepted);
    return 0;
}
