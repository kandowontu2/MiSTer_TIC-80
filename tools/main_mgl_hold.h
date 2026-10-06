/* Hold a TIC-80 cached runtime until the first delayed MGL cart dispatch ends.
 * Saved/user reset state remains independent of this temporary wire overlay. */
#ifndef TIC80_MAIN_MGL_HOLD_H
#define TIC80_MAIN_MGL_HOLD_H
#include <stdint.h>
static inline int tm_main_mgl_initial_cart(int count,int current,int action,int type,unsigned index)
{
    return count>0 && current==0 && action==0 && type=='F' && (index==0 || index==0x40);
}
static inline int tm_main_mgl_hold(int count,int current,int done,int state,int action,int type,unsigned index)
{
    return !done && state>=0 && state<3 && tm_main_mgl_initial_cart(count,current,action,type,index);
}
static inline uint16_t tm_main_mgl_status_word(uint16_t saved,unsigned word,int hold)
{
    return saved | (word==0 && hold ? 1u : 0u);
}
#endif
