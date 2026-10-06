/* Companion for the pinned Main_MiSTer patch. Source metadata is a separate
 * FIO download at index 0xfe; it never changes the cart bytes or extension. */
#ifndef TIC80_MAIN_SOURCE_PACKET_H
#define TIC80_MAIN_SOURCE_PACKET_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tic80_mister/memory_map.h"
static inline size_t tm_main_source_packet(const char *path,unsigned index,uint8_t *out)
{
    if(index!=0 && index!=0x40) return 0;
    size_t length=path?strlen(path)+1:0;
    if(!path || *path!='/' || length<2 || length>TM_CART_SOURCE_CAPACITY) length=0;
    for(unsigned i=0;i<4;++i) out[i]=(uint8_t)(TM_CART_SOURCE_MAGIC>>(8*i));
    out[4]=(uint8_t)index; out[5]=0;
    out[6]=(uint8_t)length; out[7]=(uint8_t)(length>>8);
    if(length) memcpy(out+8,path,length);
    return 8+length;
}
#endif
