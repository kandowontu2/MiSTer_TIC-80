"""Create TIC-80 MiSTer v3 controller maps with keyboard actions.

Use a controller's saved global v3 map as --system and an existing TIC-80
map as --existing when available. This writes only the requested local output;
installation must preserve a backup and reload the core to reread the map.
"""
import argparse
from pathlib import Path
import struct

KEY_EMU = 768  # Linux KEY_MAX + 1, as used by the pinned MiSTer Main.


def decode(data):
    if len(data) != 128:
        raise ValueError('Expected a 128-byte MiSTer v3 controller map')
    return list(struct.unpack('<32I', data))


def create(system, existing=None, axes=(0, 1)):
    source = decode(system)
    result = decode(existing) if existing is not None else source.copy()
    # Main's default core mapping adds analog directions as second bindings
    # for the D-pad. Reserve the requested left stick for WASD instead.
    x, y = axes
    if not (0 <= x <= 63 and 0 <= y <= 63 and x != y):
        raise ValueError('Expected two distinct Linux absolute-axis codes')
    direction_codes = {KEY_EMU + x*2, KEY_EMU + x*2+1,
                       KEY_EMU + y*2, KEY_EMU + y*2+1}
    for i in range(4):
        low, high = result[i] & 65535, result[i] >> 16
        if low in direction_codes: low = 0
        if high in direction_codes: high = 0
        result[i] = low | high << 16
    result[8:16] = [KEY_EMU+y*2, KEY_EMU+x*2, KEY_EMU+y*2+1,
                   KEY_EMU+x*2+1, source[11], source[10],source[8],source[9]]
    # Preserve the existing four face actions; use global A/B/X/Y for a new map.
    return struct.pack('<32I', *result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--system', required=True, type=Path)
    parser.add_argument('--existing', type=Path)
    parser.add_argument('--axes', nargs=2, type=int, default=(0, 1),
                        help='Linux left-stick X/Y axes; Xbox defaults are 0 1')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.write_bytes(create(args.system.read_bytes(),
                                  args.existing.read_bytes() if args.existing else None,
                                  args.axes))
