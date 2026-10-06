"""Moving raster with frame IDs, plus an independent pixel/tearing oracle."""
from pathlib import Path

CODE = b'''-- script: lua
-- title: MiSTer motion and frame integrity
t=0
function BOOT()
 local colors={[0]={0,0,0},[1]={64,64,64},[11]={255,64,0},[12]={255,255,255}}
 for c,rgb in pairs(colors) do
  for channel=1,3 do poke(0x3fc0+c*3+channel-1,rgb[channel]) end
 end
 poke(0x3ff8,0)
end
function TIC()
 local n=t%16777216
 cls(0)
 for y=8,120,8 do
  for x=0,232,8 do
   if (x//8+y//8)%2==0 then rect(x,y,8,8,1) end
  end
 end
 rect(n%237,8,4,120,11)
 for bit=0,23 do
  local c=((n>>bit)&1)==1 and 12 or 0
  rect(bit*10,0,10,8,c)
  rect(bit*10,128,10,8,c)
 end
 t=t+1
end
'''


def cartridge():
    assert len(CODE) <= 65535
    return bytes([17, 0, 0, 0, 5, len(CODE) & 255, len(CODE) >> 8, 0]) + CODE


def reference_rgb(frame):
    assert 0 <= frame < 1 << 24
    image = bytearray(256 * 144 * 3)
    for y in range(136):
        for x in range(240):
            if y < 8 or y >= 128:
                value = 255 if frame & (1 << (x // 10)) else 0
                pixel = (value,) * 3
            elif frame % 237 <= x < frame % 237 + 4:
                pixel = (255, 64, 0)
            else:
                value = 64 if (x // 8 + y // 8) % 2 == 0 else 0
                pixel = (value,) * 3
            offset = ((y + 4) * 256 + x + 8) * 3
            image[offset:offset + 3] = bytes(pixel)
    return bytes(image)


def inspect_rgb(data):
    assert len(data) == 256 * 144 * 3, 'Unexpected capture geometry'
    frame = 0
    for bit in range(24):
        offset = (8 * 256 + bit * 10 + 13) * 3
        pixel = data[offset:offset + 3]
        assert pixel in (b'\0\0\0', b'\xff\xff\xff'), ('Invalid frame code', bit, pixel)
        if pixel[0]:
            frame |= 1 << bit
    expected = reference_rgb(frame)
    mismatches = sum(left != right for left, right in zip(data, expected))
    assert mismatches == 0, dict(frame=frame, mismatching_channels=mismatches)
    return frame


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(cartridge())
    print('Motion/tearing cartridge:', args.output)
