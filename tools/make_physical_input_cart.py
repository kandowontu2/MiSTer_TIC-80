"""Create a fresh-save cartridge for human-operated controller/mouse checks."""
import argparse
from pathlib import Path
import subprocess
import uuid

p = argparse.ArgumentParser()
p.add_argument('--player', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
folder = root / 'build/physical-input'
folder.mkdir(parents=True, exist_ok=True)
saveid = 'tic80-physical-' + uuid.uuid4().hex
code = '''-- script: lua
-- title: Physical input diagnostic
-- saveid: SAVEID
local previous=0
local names={'UP','DOWN','LEFT','RIGHT','A','B','X','Y'}
function BOOT()
 local x,y=mouse()
 pmem(0,pmem(0)+1)
 pmem(8,x); pmem(9,y)
end
function TIC()
 local held=btn()&255
 pmem(1,pmem(1)|held); pmem(2,held)
 for i=0,7 do
  if (held&(1<<i))~=0 and (previous&(1<<i))==0 then
   pmem(20+i,pmem(20+i)+1)
  end
 end
 previous=held
 local x,y,l,m,r,sx,sy=mouse()
 local buttons=(l and 1 or 0)|(m and 2 or 0)|(r and 4 or 0)
 pmem(3,pmem(3)|buttons); pmem(4,buttons)
 if sy>0 then pmem(5,pmem(5)+sy) end
 if sy<0 then pmem(6,pmem(6)-sy) end
 if x~=pmem(8) then pmem(10,pmem(10)+1) end
 if y~=pmem(9) then pmem(11,pmem(11)+1) end
 pmem(8,x); pmem(9,y)
 pmem(12,held==0 and buttons==0 and not key() and pmem(12)+1 or 0)
 pmem(13,pmem(13)+1)
 cls(0)
 print('MiSTer physical input check',8,5,12)
 for i=0,7 do
  local seen=(pmem(1)&(1<<i))~=0
  local down=(held&(1<<i))~=0
  local px=8+(i%4)*58; local py=22+(i//4)*20
  rect(px,py,54,16,down and 12 or seen and 5 or 2)
  print(names[i+1],px+4,py+5,0)
 end
 print('Mouse buttons '..pmem(3)..'/7',8,65,12)
 print('Wheel up '..pmem(5)..' down '..pmem(6),8,77,12)
 print('Movement X '..pmem(10)..' Y '..pmem(11),8,89,12)
 print('Release all: '..pmem(12)..' ticks',8,101,12)
 print('Press directions and A B X Y',8,117,11)
 rect(x,y,3,3,15)
end
'''.replace('SAVEID', saveid).encode()
cart = folder / 'diagnostic.tic'
cart.write_bytes(bytes([17,0,0,0,5,len(code)&255,len(code)>>8,0])+code)
(folder / 'saveid.txt').write_text(saveid+'\n')
subprocess.run([a.player,str(cart),'180',str(folder/'idle.rgba'),str(folder/'idle.s16le')],check=True)
print('Fresh physical-input diagnostic: '+saveid)
