#include "tic80_mister/cart_file.h"
#include "tic80_mister/cart.h"
#include <png.h>
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT (4u * 1024u * 1024u)
#define MAX_SIDE 4096u
#define MAX_PIXELS (4u * 1024u * 1024u)
static const uint8_t signature[8]={137,80,78,71,13,10,26,10};
typedef struct {
    const uint8_t *cart;
    size_t cart_size;
    uint32_t width,height;
} envelope;
static uint32_t big32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static int is_png(const uint8_t *bytes,size_t size) { return bytes && size>=8 && !memcmp(bytes,signature,8); }
static int png_envelope(const uint8_t *bytes,size_t size,envelope *out)
{
    memset(out,0,sizeof *out);
    if (!is_png(bytes,size) || size>LIMIT) return -1;
    size_t offset=8;
    int header=0,palette=0,data=0,data_ended=0,cart=0;
    unsigned color=0;
    while (offset<size) {
        if (size-offset<12) return -1;
        uint32_t length=big32(bytes+offset);
        const uint8_t *type=bytes+offset+4,*payload=type+4;
        if (length>size-offset-12) return -1;
        for (unsigned i=0;i<4;++i)
            if (!((type[i]>='A' && type[i]<='Z') || (type[i]>='a' && type[i]<='z'))) return -1;
        if (type[2]&32) return -1; /* PNG's reserved chunk-name bit. */
        if ((uint32_t)crc32(0,type,(uInt)length+4)!=big32(payload+length)) return -1;
        if (!header && memcmp(type,"IHDR",4)) return -1;
        if (!memcmp(type,"IHDR",4)) {
            if (header || length!=13) return -1;
            out->width=big32(payload); out->height=big32(payload+4);
            if (!out->width || !out->height || out->width>MAX_SIDE || out->height>MAX_SIDE ||
                out->width>MAX_PIXELS/out->height) return -1;
            unsigned depth=payload[8]; color=payload[9];
            int valid=(color==0 && (depth==1 || depth==2 || depth==4 || depth==8 || depth==16)) ||
                (color==3 && (depth==1 || depth==2 || depth==4 || depth==8)) ||
                ((color==2 || color==4 || color==6) && (depth==8 || depth==16));
            if (!valid || payload[10] || payload[11] || payload[12]>1) return -1;
            header=1;
        } else if (!memcmp(type,"PLTE",4)) {
            if (palette || data || !length || length>768 || length%3 || color==0 || color==4) return -1;
            palette=1;
        } else if (!memcmp(type,"IDAT",4)) {
            if (data_ended || (color==3 && !palette)) return -1;
            data=1;
        } else if (!memcmp(type,"IEND",4)) {
            return length || !data || offset+12!=size ? -1 : 0;
        } else if (!(type[0]&32)) return -1; /* Unknown critical chunk. */
        if (data && memcmp(type,"IDAT",4)) data_ended=1;
        if (!memcmp(type,"caRt",4)) {
            if (cart) return -1;
            cart=1; out->cart=payload; out->cart_size=length;
        }
        offset+=length+12;
    }
    return -1;
}
int tm_cart_file_validate(const uint8_t *bytes,size_t size)
{
    if (!bytes || size<4 || size>LIMIT) return -1;
    if (!is_png(bytes,size)) return tm_cart_validate(bytes,size);
    envelope png;
    return png_envelope(bytes,size,&png);
}
typedef struct {
    const uint8_t *bytes;
    size_t size,offset;
    png_structp png;
    png_infop info;
    uint8_t *rgba;
    png_bytep *rows;
} image_reader;
static void read_png(png_structp png,png_bytep out,png_size_t size)
{
    image_reader *reader=png_get_io_ptr(png);
    if (size>reader->size-reader->offset) png_error(png,"Truncated cartridge image");
    memcpy(out,reader->bytes+reader->offset,size); reader->offset+=size;
}
static void png_failure(png_structp png,png_const_charp message) { (void)message; png_longjmp(png,1); }
static void cart_png_warning(png_structp png,png_const_charp message) { (void)png; (void)message; }
static int rgba_image(const uint8_t *bytes,size_t size,const envelope *env,uint8_t **out)
{
    int result=-1;
    /* Allocations live on the heap: cleanup remains defined after longjmp. */
    image_reader *reader=calloc(1,sizeof *reader);
    if (!reader) return -1;
    reader->bytes=bytes; reader->size=size;
    reader->png=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,png_failure,cart_png_warning);
    if (!reader->png) goto done;
    if (setjmp(png_jmpbuf(reader->png))) goto done;
    reader->info=png_create_info_struct(reader->png);
    if (!reader->info) goto done;
    png_set_read_fn(reader->png,reader,read_png);
    png_set_user_limits(reader->png,MAX_SIDE,MAX_SIDE);
    png_set_chunk_malloc_max(reader->png,LIMIT);
    png_set_crc_action(reader->png,PNG_CRC_ERROR_QUIT,PNG_CRC_ERROR_QUIT);
    png_read_info(reader->png,reader->info);
    int depth=png_get_bit_depth(reader->png,reader->info),color=png_get_color_type(reader->png,reader->info);
    if (png_get_image_width(reader->png,reader->info)!=env->width || png_get_image_height(reader->png,reader->info)!=env->height) goto done;
    if (depth==16) png_set_strip_16(reader->png);
    if (color==PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(reader->png);
    if (color==PNG_COLOR_TYPE_GRAY && depth<8) png_set_expand_gray_1_2_4_to_8(reader->png);
    int transparent=png_get_valid(reader->png,reader->info,PNG_INFO_tRNS);
    if (transparent) png_set_tRNS_to_alpha(reader->png);
    if (!(color&PNG_COLOR_MASK_ALPHA) && !transparent) png_set_filler(reader->png,255,PNG_FILLER_AFTER);
    if (color==PNG_COLOR_TYPE_GRAY || color==PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(reader->png);
    png_set_interlace_handling(reader->png);
    png_read_update_info(reader->png,reader->info);
    if (png_get_rowbytes(reader->png,reader->info)!=env->width*4u || png_get_channels(reader->png,reader->info)!=4) goto done;
    reader->rgba=malloc((size_t)env->width*env->height*4);
    reader->rows=malloc((size_t)env->height*sizeof *reader->rows);
    if (!reader->rgba || !reader->rows) goto done;
    for (uint32_t y=0;y<env->height;++y) reader->rows[y]=reader->rgba+(size_t)y*env->width*4;
    png_read_image(reader->png,reader->rows);
    png_read_end(reader->png,reader->info);
    *out=reader->rgba; reader->rgba=NULL; result=0;
done:
    if (reader->png) png_destroy_read_struct(&reader->png,&reader->info,NULL);
    free(reader->rgba); free(reader->rows); free(reader);
    return result;
}
static int legacy_payload(const uint8_t *rgba,size_t size,uint8_t **payload,size_t *length)
{
    if (size<8) return -1;
    uint8_t header[4];
    for (unsigned i=0;i<4;++i) header[i]=(rgba[i*2]&15) | ((rgba[i*2+1]&15)<<4);
    unsigned bits=header[0];
    size_t bytes=header[1] | (size_t)header[2]<<8 | (size_t)header[3]<<16;
    if (!bits || bits>8 || !bytes || bytes>LIMIT || bytes*8>(size-8)*bits) return -1;
    uint8_t *out=malloc(bytes); if (!out) return -1;
    size_t channel=8; uint32_t accumulator=0; unsigned have=0,mask=(1u<<bits)-1;
    for (size_t i=0;i<bytes;++i) {
        while (have<8) { accumulator|=(rgba[channel++]&mask)<<have; have+=bits; }
        out[i]=(uint8_t)accumulator; accumulator>>=8; have-=8;
    }
    *payload=out; *length=bytes; return 0;
}
static int inflate_cart(const uint8_t *payload,size_t length,uint8_t **native,size_t *size)
{
    uint8_t *out=malloc(LIMIT); if (!out) return -1;
    z_stream stream={0};
    stream.next_in=(Bytef *)payload; stream.avail_in=(uInt)length;
    stream.next_out=out; stream.avail_out=LIMIT;
    int result=-1;
    if (inflateInit(&stream)==Z_OK) {
        int status=inflate(&stream,Z_FINISH);
        if (status==Z_STREAM_END && !stream.avail_in && !tm_cart_validate(out,stream.total_out)) {
            *native=out; *size=stream.total_out; out=NULL; result=0;
        }
        inflateEnd(&stream);
    }
    free(out); return result;
}
int tm_cart_file_decode(const uint8_t *bytes,size_t size,uint8_t **native,size_t *native_size)
{
    *native=NULL; *native_size=0;
    if (tm_cart_file_validate(bytes,size)) return -1;
    if (!is_png(bytes,size)) {
        uint8_t *copy=malloc(size); if (!copy) return -1;
        memcpy(copy,bytes,size); *native=copy; *native_size=size; return 0;
    }
    envelope png; if (png_envelope(bytes,size,&png)) return -1;
    uint8_t *rgba=NULL,*legacy=NULL;
    if (rgba_image(bytes,size,&png,&rgba)) return -1;
    const uint8_t *payload=png.cart; size_t length=png.cart_size;
    if (!length && legacy_payload(rgba,(size_t)png.width*png.height*4,&legacy,&length)) { free(rgba); return -1; }
    if (legacy) payload=legacy;
    int result=inflate_cart(payload,length,native,native_size);
    free(legacy); free(rgba); return result;
}
