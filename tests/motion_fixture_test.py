"""The frame oracle rejects a mixed raster and a one-pixel color error."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from motion_fixture import inspect_rgb, reference_rgb

for frame in (0, 1, 37, 236, 237, 238, 65536, 0xffffff):
    assert inspect_rgb(reference_rgb(frame)) == frame
old = reference_rgb(37)
new = reference_rgb(38)
for bad in (old[:72*256*3] + new[72*256*3:], old[:5000] + bytes([old[5000] ^ 1]) + old[5001:]):
    try:
        inspect_rgb(bad)
    except AssertionError:
        pass
    else:
        raise AssertionError('Mixed frame or altered pixel was accepted')
print('Frame oracle accepts exact rasters and rejects mixed frames and individual RGB errors')
