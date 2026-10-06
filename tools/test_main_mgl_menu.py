"""Exercise the actual staged Main timer/dispatch branches with an info popup."""
import argparse
import re
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--menu-source',type=Path,help='Archived Main source for the failing control')
p.add_argument('--keep',type=Path)
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='tic80-menu-') as temporary:
    work=Path(temporary)
    subprocess.run(['python3',str(ROOT/'tools/stage_main_source.py'),'--output',str(work)],check=True)
    code=(a.menu_source or work/'menu.cpp').read_text()
    begin=code.index('\t\tcase 0:',code.index('if (!mgl->done)'))
    end=code.index('\t\tcase 3:',begin)
    branches=code[begin:end]
    pinned=(ROOT/'reference/main/menu.cpp').read_text()
    states=re.search(r'enum MENU\s*\{(.*?)\};',pinned,re.S).group(1)
    action=re.search(r'^#define MGL_ACTION_LOAD\s+(\d+)',(ROOT/'reference/main/support/arcade/mra_loader.h').read_text(),re.M).group(1)
    test=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "main_mgl_hold.h"
enum MENU {MENU_STATES};
enum {MGL_ACTION_LOAD=LOAD_ACTION};
struct Item {int action,type; unsigned index;};
struct Mgl {int count,current,done,state; unsigned timer; Item item[2];};
static unsigned now;
static unsigned menustate;
static const char *core;
static int CheckTimer(unsigned deadline) {return now>=deadline;}
static const char *user_io_get_core_name() {return core;}
static void step(Mgl *mgl) {
 if(!mgl->done) switch(mgl->state) {
 BRANCHES
 }
}
int main() {
 (void)user_io_get_core_name;
 for(unsigned index=0;index<256;++index)
 for(int action=0;action<2;++action)
 for(int type: {'F','S','f'})
 for(int current=0;current<2;++current)
 for(int done=0;done<2;++done)
 for(unsigned menu=0;menu<8;++menu)
 for(const char *name: {"TIC-80","tic-80","OTHER"}) {
  Mgl m={2,current,done,0,3000,{{action,type,index},{action,type,index}}};
  core=name; menustate=menu; now=2999; step(&m);
  assert(m.state==0 && menustate==menu);
  now=3000; step(&m);
  assert(m.state==(done?0:action?4:1) && menustate==menu);
  step(&m);
  bool dismiss=!done && !action && current==0 && type=='F' &&
    (index==0 || index==64) && !strcasecmp(name,"TIC-80") && menu==MENU_INFO;
  assert(menustate==(dismiss?unsigned(MENU_NONE1):menu));
  assert(m.timer==3000);
 }
 puts("Actual Main timer/dispatch: first TIC native/PNG popup yields after delay; later files, other actions/cores/UI preserved");
}
'''.replace('BRANCHES',branches).replace('MENU_STATES',states).replace('LOAD_ACTION',action).replace('#include <cstdio>','#include <cstdio>\n#include <initializer_list>')
    (work/'test.cpp').write_text(test)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(work),str(work/'test.cpp'),'-o',str(work/'test')],check=True)
    if a.keep:
        a.keep.mkdir(parents=True,exist_ok=False)
        import shutil
        for name in ('test.cpp','test','menu.cpp','main_mgl_hold.h'): shutil.copyfile(work/name,a.keep/name)
    subprocess.run([str(work/'test')],check=True)
