#include "../tools/main_mgl_hold.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    for(unsigned index=0;index<256;++index) {
        for(int state=0;state<6;++state) {
            int expected=(index==0 || index==0x40) && state<3;
            assert(tm_main_mgl_hold(1,0,0,state,0,'F',index)==expected);
            assert(tm_main_mgl_hold(1,1,0,state,0,'F',index)==0);
            assert(tm_main_mgl_hold(0,0,0,state,0,'F',index)==0);
            assert(tm_main_mgl_hold(1,0,1,state,0,'F',index)==0);
            assert(tm_main_mgl_hold(1,0,0,state,1,'F',index)==0);
            assert(tm_main_mgl_hold(1,0,0,state,0,'s',index)==0);
        }
    }
    for(unsigned word=0;word<8;++word) {
        for(unsigned saved=0;saved<65536;++saved) {
            assert(tm_main_mgl_status_word(saved,word,0)==saved);
            assert(tm_main_mgl_status_word(saved,word,1)==(saved | (word==0?1u:0u)));
        }
    }
    assert(!tm_main_mgl_hold(1,0,0,-1,0,'F',0));
    puts("MGL hold: initial TIC/PNG file only, completion/error release, later actions independent, saved reset/all status bits preserved");
}
