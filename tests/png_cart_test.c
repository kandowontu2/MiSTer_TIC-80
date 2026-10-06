#include "tic80_mister/cart_file.h"
#include "tic80_mister/vm.h"
#include "tic80_mister/pmem.h"
#include "tic80.h"
#include "tic.h"
#include "cart.h"
#include "tools.h"
#include "script.h"
#include "ext/png.h"
#include <png.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static const char *export_directory;
static uint32_t big32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p,uint32_t value) { p[0]=value>>24; p[1]=value>>16; p[2]=value>>8; p[3]=value; }
static png_buffer zip(const uint8_t *bytes,size_t size)
{
    uLongf capacity=compressBound(size); png_buffer out={malloc(capacity),0}; CHECK(out.data);
    CHECK(compress2(out.data,&capacity,bytes,size,Z_BEST_COMPRESSION)==Z_OK); out.size=capacity; return out;
}
static png_buffer cover(unsigned width,unsigned height)
{
    png_img img={(s32)width,(s32)height,{0}};
    img.data=malloc((size_t)width*height*4); CHECK(img.data);
    memset(img.data,0x88,(size_t)width*height*4);
    png_buffer result=png_write(img,(png_buffer){0}); free(img.data); CHECK(result.data); return result;
}
static void image_write(png_structp png,png_bytep bytes,png_size_t length)
{
    png_buffer *out=png_get_io_ptr(png);
    CHECK(length<1024*1024 && (size_t)out->size+length<1024*1024);
    out->data=realloc(out->data,out->size+length); CHECK(out->data);
    memcpy(out->data+out->size,bytes,length); out->size+=length;
}
static void image_flush(png_structp png) { (void)png; }
static png_buffer color_cover(unsigned color,unsigned depth,int interlace,int transparent)
{
    png_buffer out={0};
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL); CHECK(png);
    png_infop info=png_create_info_struct(png); CHECK(info);
    CHECK(!setjmp(png_jmpbuf(png)));
    png_set_write_fn(png,&out,image_write,image_flush);
    png_set_IHDR(png,info,8,8,depth,color,interlace,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_color palette[2]={{0,0,0},{255,255,255}};
    if (color==PNG_COLOR_TYPE_PALETTE) png_set_PLTE(png,info,palette,2);
    if (transparent) {
        png_byte alpha[2]={0,255}; png_color_16 key={0};
        png_set_tRNS(png,info,alpha,color==PNG_COLOR_TYPE_PALETTE ? 2 : 0,&key);
    }
    png_write_info(png,info);
    uint8_t pixels[8][64]={{0}}; png_bytep rows[8];
    for (unsigned y=0;y<8;++y) rows[y]=pixels[y];
    png_write_image(png,rows); png_write_end(png,info);
    png_destroy_write_struct(&png,&info); return out;
}
static png_buffer strip(png_buffer in,const char *type)
{
    png_buffer out={malloc(in.size),8}; CHECK(out.data); memcpy(out.data,in.data,8);
    for (size_t offset=8;offset<(size_t)in.size;) {
        size_t length=big32(in.data+offset)+12;
        if (memcmp(in.data+offset+4,type,4)) { memcpy(out.data+out.size,in.data+offset,length); out.size+=length; }
        offset+=length;
    }
    return out;
}
static png_buffer add(png_buffer in,const char *type,const uint8_t *payload,size_t length)
{
    png_buffer out={malloc(in.size+length+12),in.size+length+12}; CHECK(out.data);
    size_t end=in.size-12; memcpy(out.data,in.data,end); put32(out.data+end,length);
    memcpy(out.data+end+4,type,4); if (length) memcpy(out.data+end+8,payload,length);
    put32(out.data+end+8+length,crc32(0,out.data+end+4,length+4));
    memcpy(out.data+end+12+length,in.data+end,12); return out;
}
static void same(png_buffer png,const uint8_t *native,size_t size)
{
    uint8_t *out=NULL; size_t count=0;
    CHECK(!tm_cart_file_validate(png.data,png.size));
    CHECK(!tm_cart_file_decode(png.data,png.size,&out,&count));
    CHECK(count==size && !memcmp(out,native,size)); free(out);
}
static void bad(png_buffer png)
{
    uint8_t *out=(void *)1; size_t size=123;
    CHECK(tm_cart_file_decode(png.data,png.size,&out,&size)==-1);
    CHECK(!out && !size);
}
static void write_file(const char *name,png_buffer data)
{
    if (!export_directory) return;
    char path[1024]; snprintf(path,sizeof path,"%s/%s",export_directory,name);
    FILE *file=fopen(path,"wb"); CHECK(file);
    CHECK(fwrite(data.data,1,data.size,file)==(size_t)data.size); CHECK(!fclose(file));
}
static void demos(void)
{
    png_buffer image=cover(128,128);
    uint8_t *native=malloc(sizeof(tic_cartridge)); CHECK(native);
    FOREACH_LANG(script) {
        s32 size=tic_tool_unzip(native,sizeof(tic_cartridge),script->demo.data,script->demo.size); CHECK(size>0);
        png_buffer compressed=zip(native,size),modern=png_encode(image,compressed),legacy=strip(modern,"caRt");
        same(modern,native,size); same(legacy,native,size);
        /* Verify interoperability with the pinned upstream PNG encoder/decoder. */
        png_buffer upstream=png_decode(legacy); CHECK(upstream.size==compressed.size && !memcmp(upstream.data,compressed.data,compressed.size)); free(upstream.data);
        tm_vm *a=NULL,*b=NULL,*c=NULL;
        CHECK(tm_vm_open(&a,native,size)==TM_VM_OK);
        CHECK(tm_vm_open(&b,modern.data,modern.size)==TM_VM_OK);
        CHECK(tm_vm_open(&c,legacy.data,legacy.size)==TM_VM_OK);
        CHECK(!strcmp(tm_vm_key(a),tm_vm_key(b)) && !strcmp(tm_vm_key(a),tm_vm_key(c)));
        for (unsigned frame=0;frame<30;++frame) {
            tic80_input input={0}; if (frame==1) input.gamepads.data=8;
            CHECK(tm_vm_tick(a,input,0)==TM_VM_OK && tm_vm_tick(b,input,0)==TM_VM_OK && tm_vm_tick(c,input,0)==TM_VM_OK);
            CHECK(!memcmp(tm_vm_product(a)->screen,tm_vm_product(b)->screen,TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4));
            CHECK(!memcmp(tm_vm_product(a)->screen,tm_vm_product(c)->screen,TIC80_FULLWIDTH*TIC80_FULLHEIGHT*4));
            CHECK(!memcmp(tm_vm_product(a)->samples.buffer,tm_vm_product(b)->samples.buffer,1600*sizeof(s16)));
            CHECK(!memcmp(tm_vm_product(a)->samples.buffer,tm_vm_product(c)->samples.buffer,1600*sizeof(s16)));
        }
        tm_vm_close(a); tm_vm_close(b); tm_vm_close(c);
        char name[96]; snprintf(name,sizeof name,"%s.png",script->name); write_file(name,modern);
        snprintf(name,sizeof name,"%s-legacy.png",script->name); write_file(name,legacy);
        printf("%s: native/chunk-PNG/legacy-PNG byte, key, video and PCM parity passed\n",script->name);
        free(compressed.data); free(modern.data); free(legacy.data);
    }
    free(image.data); free(native);
}
static png_buffer steg(png_buffer compressed,unsigned bits)
{
    unsigned width=64,height=64; size_t channels=width*height*4;
    CHECK((size_t)compressed.size*8<=(channels-8)*bits);
    uint8_t *rgba=calloc(channels,1); CHECK(rgba);
    uint8_t header[4]={bits,compressed.size,compressed.size>>8,compressed.size>>16};
    for (unsigned i=0;i<8;++i) rgba[i]=(header[i/2]>>(i%2*4))&15;
    for (size_t bit=0;bit<(size_t)compressed.size*8;++bit)
        rgba[8+bit/bits]|=((compressed.data[bit/8]>>(bit%8))&1)<<(bit%bits);
    png_buffer result=png_write((png_img){width,height,{.data=rgba}},(png_buffer){0}); free(rgba); return result;
}
static void bounds(void)
{
    const uint8_t native[]={17,0,0,0,5,25,0,0,'f','u','n','c','t','i','o','n',' ','T','I','C','(',')',' ','c','l','s','(','2',')',' ','e','n','d'};
    png_buffer compressed=zip(native,sizeof native),image=cover(1,1),modern=add(image,"caRt",compressed.data,compressed.size);
    same(modern,native,sizeof native);
    const unsigned colors[]={PNG_COLOR_TYPE_GRAY,PNG_COLOR_TYPE_RGB,PNG_COLOR_TYPE_PALETTE,
                             PNG_COLOR_TYPE_GRAY_ALPHA,PNG_COLOR_TYPE_RGB_ALPHA};
    unsigned images=0;
    for (unsigned c=0;c<5;++c) for (unsigned depth=1;depth<=16;depth*=2) {
        if (colors[c]==PNG_COLOR_TYPE_PALETTE ? depth==16 :
            (colors[c]!=PNG_COLOR_TYPE_GRAY && depth<8)) continue;
        for (int interlace=0;interlace<=1;++interlace) for (int transparent=0;transparent<=1;++transparent) {
            if (transparent && (colors[c]&PNG_COLOR_MASK_ALPHA)) continue;
            png_buffer cover=color_cover(colors[c],depth,interlace,transparent);
            png_buffer cart=add(cover,"caRt",compressed.data,compressed.size);
            same(cart,native,sizeof native); ++images;
            free(cover.data); free(cart.data);
        }
    }
    printf("%u grayscale, RGB, palette, alpha, 16-bit and interlaced PNG covers passed\n",images);
    for (int size=0;size<modern.size;++size) bad((png_buffer){modern.data,size});
    png_buffer duplicate=add(modern,"caRt",compressed.data,compressed.size); bad(duplicate); free(duplicate.data);
    png_buffer unknown=add(image,"BOOM",compressed.data,compressed.size); bad(unknown); free(unknown.data);
    png_buffer trailing={malloc(modern.size+1),modern.size+1}; memcpy(trailing.data,modern.data,modern.size); trailing.data[modern.size]=0; bad(trailing); free(trailing.data);
    modern.data[29]^=1; bad(modern); modern.data[29]^=1; /* IHDR CRC. */
    png_buffer plain=cover(1,1); bad(plain); free(plain.data);
    for (unsigned bits=1;bits<=8;++bits) {
        png_buffer legacy=steg(compressed,bits); same(legacy,native,sizeof native);
        png_buffer empty=add(legacy,"caRt",NULL,0); same(empty,native,sizeof native); free(empty.data);
        free(legacy.data);
    }
    uint8_t channels[64]={0}; png_img small={4,4,{.data=channels}};
    for (unsigned bits=0;bits<=9;bits+=9) {
        channels[0]=bits; channels[2]=1;
        png_buffer invalid=png_write(small,(png_buffer){0}); bad(invalid); free(invalid.data);
    }
    channels[0]=1; channels[2]=15; channels[3]=15; /* Claimed payload exceeds available channels. */
    png_buffer short_hidden=png_write(small,(png_buffer){0}); bad(short_hidden); free(short_hidden.data);
    uint8_t *extra=malloc(compressed.size+1); CHECK(extra); memcpy(extra,compressed.data,compressed.size); extra[compressed.size]=0;
    png_buffer ztrailing=add(image,"caRt",extra,compressed.size+1); bad(ztrailing); free(ztrailing.data); free(extra);
    png_buffer zshort=add(image,"caRt",compressed.data,compressed.size-1); bad(zshort); free(zshort.data);
    /* Modern payload takes precedence over a different legacy payload. */
    uint8_t alternative[sizeof native]; memcpy(alternative,native,sizeof native); alternative[28]='3';
    png_buffer other_zip=zip(alternative,sizeof alternative),legacy=steg(compressed,3),precedence=add(legacy,"caRt",other_zip.data,other_zip.size);
    same(precedence,alternative,sizeof alternative); free(precedence.data); free(legacy.data); free(other_zip.data);
    /* Corrupt an IDAT stream while retaining its chunk CRC: envelope parsing
     * succeeds, but bounded libpng decoding must return an error. */
    png_buffer corrupt={malloc(modern.size),modern.size}; memcpy(corrupt.data,modern.data,modern.size);
    for (size_t at=8;at<(size_t)corrupt.size;at+=big32(corrupt.data+at)+12)
        if (!memcmp(corrupt.data+at+4,"IDAT",4)) {
            size_t n=big32(corrupt.data+at); memset(corrupt.data+at+8,0,n);
            put32(corrupt.data+at+8+n,crc32(0,corrupt.data+at+4,n+4)); break;
        }
    CHECK(!tm_cart_file_validate(corrupt.data,corrupt.size)); bad(corrupt); write_file("corrupt-image.png",corrupt); free(corrupt.data);
    /* Native payload must itself pass bounds, including the four-byte PNG
     * prefix that would enter upstream's unchecked recursive PNG loader. */
    const uint8_t invalids[][8]={{5,0,0,0},{137,'P','N','G',0,0,0,0},{19|(4<<5),1,0,0,1},{5,1,0,0,'a',0,0,1}};
    for (unsigned i=0;i<sizeof invalids/sizeof *invalids;++i) {
        png_buffer z=zip(invalids[i],sizeof invalids[i]),png=add(image,"caRt",z.data,z.size); bad(png); free(png.data); free(z.data);
    }
    uint8_t *huge=calloc(4u*1024u*1024u+1,1); CHECK(huge);
    png_buffer max=zip(huge,4u*1024u*1024u),png_max=add(image,"caRt",max.data,max.size); same(png_max,huge,4u*1024u*1024u);
    png_buffer over=zip(huge,4u*1024u*1024u+1),png_over=add(image,"caRt",over.data,over.size); bad(png_over); write_file("oversize-native.png",png_over);
    free(huge); free(max.data); free(over.data); free(png_max.data); free(png_over.data);
    png_buffer largest=cover(4096,1024),largest_cart=add(largest,"caRt",compressed.data,compressed.size);
    same(largest_cart,native,sizeof native);
    tm_vm *image_vm=NULL; CHECK(tm_vm_open(&image_vm,largest_cart.data,largest_cart.size)==TM_VM_OK);
    CHECK(tm_vm_tick(image_vm,(tic80_input){0},0)==TM_VM_OK); tm_vm_close(image_vm);
    write_file("max-image.png",largest_cart); free(largest_cart.data); free(largest.data);
    png_buffer too_large=cover(4096,1025),large_cart=add(too_large,"caRt",compressed.data,compressed.size); bad(large_cart); free(large_cart.data); free(too_large.data);
    free(modern.data); free(image.data); free(compressed.data);
    puts("PNG CRC, truncation, structure, image stream, hidden data and decompression bounds passed");
}
int main(int argc,char **argv)
{
    if (argc>=2 && !strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    if (argc==3 && !strcmp(argv[1],"--export")) export_directory=argv[2]; else CHECK(argc==1);
    setvbuf(stdout,NULL,_IONBF,0); demos(); bounds(); return 0;
}
