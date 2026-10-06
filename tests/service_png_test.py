"""PNG loading, save continuity and decode failure through real service IPC."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
from service_recovery_test import Fixture, ordinary, save_values


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def image(width, height, rgba, payload=None, corrupt=False):
    pixels = b''.join(b'\0' + rgba[y*width*4:(y+1)*width*4] for y in range(height))
    compressed = zlib.compress(pixels) if not corrupt else b'not a zlib stream'
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', compressed) + (chunk(b'caRt', payload) if payload is not None else b'')
            + chunk(b'IEND', b''))


def modern(native, corrupt=False):
    return image(1, 1, bytes(4), zlib.compress(native), corrupt)


def legacy(native):
    payload = zlib.compress(native)
    rgba = bytearray(64*64*4)
    header = bytes([1]) + len(payload).to_bytes(3, 'little')
    for i in range(8):
        rgba[i] = header[i//2] >> ((i % 2)*4) & 15
    assert len(payload)*8 <= len(rgba)-8
    for bit in range(len(payload)*8):
        rgba[8+bit] = payload[bit//8] >> (bit % 8) & 1
    return image(64, 64, rgba)


def padded(native, size):
    out = bytearray(native)
    assert len(out) <= size
    while len(out) < size:
        left = size - len(out)
        assert left >= 4
        payload = min(left - 4, 65535)
        remaining = left - payload - 4
        if 0 < remaining < 4:
            payload -= 4 - remaining
        # Upstream ignores unknown chunk 31; traversal still checks its length.
        out.extend(bytes([31, payload & 255, payload >> 8, 0]) + bytes(payload))
    return bytes(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--service', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='tic80-png-service-') as directory:
        fixture = Fixture(Path(directory))
        native = ordinary('png-save-continuity', 2)
        fixture.actions = [
            (10, lambda: fixture.transfer(6, native)),
            (60, lambda: fixture.transfer(10, modern(native, corrupt=True))),
            (220, lambda: fixture.transfer(14, modern(native))),
            (260, lambda: fixture.transfer(18, legacy(native))),
            (310, lambda: fixture.transfer(22, modern(bytes(4*1024*1024+1)))),
            (470, lambda: fixture.transfer(26, ordinary('after-bad-png', 9))),
        ]
        try:
            process = fixture.start(args.service, 530, subprocess.PIPE, subprocess.PIPE)
            output, errors = process.communicate(timeout=15)
            output, errors = output.decode(), errors.decode()
            assert process.returncode == 0, output + errors
            assert fixture.get('CART_ACK') == 26
            assert output.count('Cartridge loaded:') == 4, output
            assert errors.count('Cartridge rejected:') == 2, errors
            assert 'Cartridge failed' not in errors, errors
            # Both rejected candidates preserve the current VM and its image.
            assert fixture.frames[40] == fixture.frames[200] == fixture.frames[450]
            assert fixture.frames[500] != fixture.frames[40]
            values = save_values(fixture.saves, 'png-save-continuity')
            assert 180 <= values[0] <= 260 and values[1] == 3, values[:2]
            after = save_values(fixture.saves, 'after-bad-png')
            assert 50 <= after[0] <= 70 and after[1] == 1, after[:2]
            assert not list(fixture.saves.glob('*.tmp-*'))
            print('Native/chunk-PNG/legacy-PNG share saves; corrupt image and oversized payload preserve the running VM; later native load succeeds')
        finally:
            fixture.close()


if __name__ == '__main__':
    main()
