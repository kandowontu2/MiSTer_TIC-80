"""Create an auto-starting music/clock/pmem soak cartridge from the pinned demo."""
import argparse
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--converter', required=True)
a = p.parse_args()
folder = ROOT / 'build/soak'
folder.mkdir(parents=True, exist_ok=True)
saveid = 'tic80-soak-' + uuid.uuid4().hex
source = (ROOT / 'reference/tic80/demos/music.lua').read_text()
source = source.replace('-- title:  Music Demo', '-- title:  MiSTer stability test')
source = '-- saveid: ' + saveid + '\n' + source
source = source.replace('t=0', 't=0\nprevious=nil\nfunction BOOT() pmem(2,pmem(2)+1) end', 1)
source = source.replace('function TIC()', 'function TIC()\n'
    ' local ms=math.floor(time()); pmem(0,t+1); pmem(1,ms)\n'
    ' if previous then pmem(3,math.max(pmem(3),ms-previous)) end; previous=ms', 1)
source = source.replace('if btnp()~=0 then music(0) end', 'if t==0 then music(0) end', 1)
source = source.replace('PRESS START TO RICH!', 'MiSTer stability test')
assert 'pmem(0,t+1)' in source and 'if t==0 then music(0) end' in source
project = folder / 'music-soak.lua'
project.write_text(source)
cartridge = folder / 'music-soak.tic'
subprocess.run([a.converter, str(project), str(cartridge)], check=True)
(folder / 'saveid.txt').write_text(saveid + '\n')
print(saveid)
