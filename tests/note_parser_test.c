#include "tic.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
extern bool parse_note(const char *,s32 *,s32 *);
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"NOTE_PARSER_FAILED line=%d input=%02x,%02x,%02x expression=%s\n",__LINE__,a,b,cbyte,#c); exit(1); } } while(0)
int main(void)
{
    unsigned a=0,b=0,cbyte=0,accepted=0,rejected=0;
    s32 note=77,octave=88;
    CHECK(!parse_note(NULL,&note,&octave) && note==77 && octave==88);
    for(a=0;a<128;++a) for(b=0;b<128;++b) for(cbyte=0;cbyte<128;++cbyte) {
        char text[]={(char)a,(char)b,(char)cbyte,0}; int base=-1;
        switch(a) { case 'C':base=0;break;case 'D':base=2;break;case 'E':base=4;break;case 'F':base=5;break;case 'G':base=7;break;case 'A':base=9;break;case 'B':base=11;break; }
        bool sharp=b=='#' && a!='E' && a!='B';
        bool expected=base>=0 && (b=='-' || sharp) && cbyte>='0' && cbyte<='8';
        note=77; octave=88; bool actual=parse_note(text,&note,&octave);
        CHECK(actual==expected);
        if(expected) { CHECK(note==base+(sharp?1:0) && octave==(int)cbyte-'1'); ++accepted; }
        else { CHECK(note==77 && octave==88); ++rejected; }
    }
    CHECK(accepted==108 && rejected==2097044);
    const char *extra[]={"","C-","C-10","C-4 ","\xff-4","C-\xff"};
    for(unsigned i=0;i<sizeof extra/sizeof *extra;++i) { note=77; octave=88; CHECK(!parse_note(extra[i],&note,&octave) && note==77 && octave==88); }
    printf("Note parser exhaustive ASCII: %u accepted, %u rejected, failed outputs unchanged, length/null/high-byte guards passed\n",accepted,rejected);
    return 0;
}
