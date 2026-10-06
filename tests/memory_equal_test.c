#define _GNU_SOURCE
#include "tic80_mister/memory_equal.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void)
{
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    assert(page>=4096);
    uint8_t *a=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    uint8_t *b=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(a!=MAP_FAILED && b!=MAP_FAILED);
    assert(!mprotect(a+page,page,PROT_NONE) && !mprotect(b+page,page,PROT_NONE));
    assert(tm_memory_equal(NULL,NULL,0));
    const size_t sizes[]={1,15,16,17,63,64,65,127,128,129,255,256,257,1023,4095,4096};
    for(unsigned i=0;i<sizeof sizes/sizeof *sizes;++i) {
        size_t size=sizes[i]; uint8_t *left=a+page-size, *right=b+page-size;
        for(size_t j=0;j<size;++j) left[j]=right[j]=(uint8_t)(j*37+11);
        assert(tm_memory_equal(left,right,size));
        // Every byte can independently make the result unequal, including
        // vector/tail boundaries. Guard pages catch reads past the valid end.
        for(size_t j=0;j<size;++j) {
            right[j]^=0x80;
            assert(!tm_memory_equal(left,right,size));
            right[j]^=0x80;
        }
    }
    // Independent source/destination alignments, without assuming either
    // pointer is aligned merely because the allocation is page aligned.
    for(unsigned x=0;x<16;++x) for(unsigned y=0;y<16;++y) {
        for(unsigned i=0;i<1025;++i) a[x+i]=b[y+i]=(uint8_t)(i*19);
        assert(tm_memory_equal(a+x,b+y,1025));
        b[y+511]^=1; assert(!tm_memory_equal(a+x,b+y,1025)); b[y+511]^=1;
        b[y+1024]^=1; assert(!tm_memory_equal(a+x,b+y,1025)); b[y+1024]^=1;
    }
    assert(!munmap(a,page*2) && !munmap(b,page*2));
    puts("Memory equality: every-byte mutations, independent alignments, tails and guard pages passed");
    return 0;
}
