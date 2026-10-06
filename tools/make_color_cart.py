"""Generate a static all-levels raster fixture and its full-color golden frame."""
import argparse
from pathlib import Path
import subprocess
import zlib
from PIL import Image, PngImagePlugin

p = argparse.ArgumentParser()
p.add_argument('--player', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
folder = root / 'build/color'
folder.mkdir(parents=True, exist_ok=True)
code = ("-- script: lua\n-- title: Full color precision\n"
        "function TIC() cls(0); poke(0x3ff8,0); "
        "for y=0,135 do for x=0,239 do pix(x,y,x%16) end end end\n"
        "function BDR(row) for i=0,15 do local r=(row*16+i)%256; "
        "poke(0x3fc0+i*3,r); poke(0x3fc1+i*3,(r+83)%256); "
        "poke(0x3fc2+i*3,(r*197+73)%256) end end\n").encode()
native = bytes([17,0,0,0,5,len(code)&255,len(code)>>8,0])+code
cart = folder / 'precision.tic'
cart.write_bytes(native)
frame = folder / 'precision.rgba'
subprocess.run([a.player,str(cart),'2',str(frame),str(folder/'precision.s16le')],check=True)
raw = frame.read_bytes()
assert len(raw)==256*144*4
for channel in range(3):
    assert set(raw[channel::4]) == set(range(256)), channel
info = PngImagePlugin.PngInfo()
info.add(b'caRt',zlib.compress(native))
Image.frombytes('RGBA',(256,144),raw).save(folder/'precision.png',pnginfo=info)
print('Native/PNG raster fixture covers every eight-bit level of all three channels')
