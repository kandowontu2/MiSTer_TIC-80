"""Checked read-only observer, torn snapshots, unsupported ABI and session loss."""
import argparse
import ctypes
import json
import mmap
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time

P = json.loads((Path(__file__).resolve().parents[1] / 'protocol/memory_map.json').read_text())
O = P['offsets']


def exercise(executable, mode):
    with tempfile.TemporaryDirectory(prefix='tic80-monitor-') as folder:
        path = Path(folder) / 'memory'
        with path.open('w+b') as file:
            file.truncate(P['region_bytes'])
            with mmap.mmap(file.fileno(), P['region_bytes']) as shared:
                def word(name, value):
                    ctypes.c_uint32.from_buffer(shared, O[name]).value = value
                word('IDENTITY', P['magic'])
                word('GEOMETRY', P['width'] | P['height'] << 16)
                word('SESSION_REQUEST', 17)
                word('SESSION_ACK', 17)
                word('AUDIO_WRITE', 4000)
                word('CART_ACK', 1234)
                word('VIDEO_PUBLISH', 26)
                word('VIDEO_PRESENTED', 26)
                shared[O['BUFFER0']:O['BUFFER0']+128] = bytes([0xA5])*128
                stopped = threading.Event()
                # The FPGA's statistics magic stays fixed while only the
                # 32-bit sequence changes. pack_into('<Q') clears all eight
                # destination bytes first, creating a spurious unsupported ABI
                # window that the real register publication does not have.
                magic = 0 if mode == 'unsupported' else P['statistics_magic']
                ctypes.c_uint32.from_buffer(shared, O['STATS_SEQUENCE']+4).value = magic
                def firmware():
                    n = 0
                    while not stopped.is_set():
                        n += 1
                        word('HEARTBEAT', 1 if mode == 'heartbeat' else n)
                        word('STATS_SEQUENCE', n*2-1)
                        word('AUDIO_STATS', n*20)
                        time.sleep(.0005)  # Widen the deliberately torn window.
                        ctypes.c_uint32.from_buffer(shared, O['AUDIO_STATS']+4).value = n*10
                        if mode != 'torn':
                            word('STATS_SEQUENCE', n*2)
                        if mode == 'session' and n == 200:
                            word('SESSION_REQUEST', 18)
                            word('SESSION_ACK', 18)
                        time.sleep(.002)
                thread = threading.Thread(target=firmware)
                thread.start()
                try:
                    result = subprocess.run([executable, '--memory', str(path), '--seconds', '2', '--interval-ms', '10'],
                                            capture_output=True, text=True, timeout=4)
                finally:
                    stopped.set()
                    thread.join()
                rows = [json.loads(line) for line in result.stdout.splitlines()]
                if mode == 'good':
                    assert result.returncode == 0 and len(rows) > 100, result.stderr
                    assert all(r['slots'] == r['underruns']*2 and r['session'] == 17 for r in rows)
                else:
                    assert result.returncode == 1, result.stderr
                    expected = {'unsupported': 'statistics unavailable', 'session': 'session changed',
                                'torn': 'No coherent active', 'heartbeat': 'heartbeat stopped'}[mode]
                    assert expected in result.stderr, result.stderr
                for name, value in [('SESSION_REQUEST', 18 if mode == 'session' else 17),
                                    ('AUDIO_WRITE', 4000), ('CART_ACK', 1234), ('VIDEO_PUBLISH', 26)]:
                    assert struct.unpack_from('<I', shared, O[name])[0] == value, name
                assert shared[O['BUFFER0']:O['BUFFER0']+128] == bytes([0xA5])*128
                print(mode + ': coherent counters, bounded failures and untouched ARM-owned words verified')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--monitor', required=True)
    args = p.parse_args()
    for mode in ('good', 'unsupported', 'session', 'torn', 'heartbeat'):
        exercise(args.monitor, mode)
