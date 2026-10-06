#include "../tools/main_source_packet.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint8_t bytes[8+TM_CART_SOURCE_CAPACITY+16];
    memset(bytes,0xcc,sizeof bytes);
    const char *path="/media/usb1/games/TIC-80/with spaces/cart.png";
    size_t size=tm_main_source_packet(path,0x40,bytes);
    assert(size==8+strlen(path)+1 && !memcmp(bytes,"TSN1",4));
    assert(bytes[4]==0x40 && !bytes[5] && bytes[6]==strlen(path)+1 && !bytes[7]);
    assert(!memcmp(bytes+8,path,strlen(path)+1));
    char longest[TM_CART_SOURCE_CAPACITY+1];
    memset(longest,'x',sizeof longest); longest[0]='/'; longest[TM_CART_SOURCE_CAPACITY-1]=0;
    assert(tm_main_source_packet(longest,0,bytes)==8+TM_CART_SOURCE_CAPACITY);
    longest[TM_CART_SOURCE_CAPACITY-1]='x'; longest[TM_CART_SOURCE_CAPACITY]=0;
    assert(tm_main_source_packet(longest,0,bytes)==8 && !bytes[6] && !bytes[7]);
    assert(tm_main_source_packet("relative.tic",0,bytes)==8 && !bytes[6] && !bytes[7]);
    assert(tm_main_source_packet(NULL,0,bytes)==8 && !bytes[6] && !bytes[7]);
    assert(tm_main_source_packet(path,1,bytes)==0);
    for(size_t i=8+TM_CART_SOURCE_CAPACITY;i<sizeof bytes;++i) assert(bytes[i]==0xcc);
    puts("Main source packet: TIC/PNG index binding, exact UTF-8 bytes, absolute path, size bounds and explicit missing-source packet passed");
    return 0;
}
