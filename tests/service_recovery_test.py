"""Exercise hung candidates, later hangs and worker crashes through real service IPC."""
import argparse
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from audio_fixture import AudioClock

PROTOCOL = json.loads((Path(__file__).resolve().parents[1] / 'protocol/memory_map.json').read_text())
OFFSETS = PROTOCOL['offsets']

def cartridge(saveid, body):
    code = (f'-- script: lua\n-- saveid: {saveid}\n' + body).encode()
    return bytes([17, 0, 0, 0, 5, len(code) & 255, len(code) >> 8, 0]) + code

def ordinary(saveid, color):
    return cartridge(saveid, 'function BOOT() pmem(1,pmem(1)+1) end\n'
                     f'function TIC() pmem(0,pmem(0)+1); cls({color}) end\n')

def save_values(folder, key):
    data = (folder / (hashlib.md5(key.encode()).hexdigest() + '.pmem')).read_bytes()
    assert len(data) == 1036 and data[:4] == b'TMPM'
    assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
    return struct.unpack_from('<256I', data, 12)

class Fixture:
    def __init__(self, root):
        self.root = root
        self.saves = root / 'saves'
        self.saves.mkdir()
        self.memory = root / 'ddr'
        self.backing = self.memory.open('w+b')
        self.backing.truncate(PROTOCOL['region_bytes'])
        self.shared = mmap.mmap(self.backing.fileno(), PROTOCOL['region_bytes'])
        self.put('IDENTITY', PROTOCOL['magic'])
        self.put('GEOMETRY', PROTOCOL['width'] | PROTOCOL['height'] << 16)
        self.put('KEYBOARD', 2)
        self.put('MOUSE', 120 | 68 << 8)
        self.frames = []
        self.events = set()
        self.actions = []
        self.hold_video = False
        self.hold_audio = False
        self.stop = threading.Event()
        self.failure = []
        self.process = None
        self.thread = threading.Thread(target=self.firmware)

    def get(self, name):
        return struct.unpack_from('<I', self.shared, OFFSETS[name])[0]

    def put(self, name, value):
        ctypes.c_uint32.from_buffer(self.shared, OFFSETS[name]).value = value & 0xffffffff

    def transfer(self, ticket, payload):
        assert self.get('CART_ACK') == ticket - 4 or ticket == 6
        self.shared[OFFSETS['CART_DATA']:OFFSETS['CART_DATA'] + len(payload)] = payload
        struct.pack_into('<I', self.shared, OFFSETS['CART_META'] + 4, len(payload))
        self.put('CART_META', ticket)

    def kill_child(self):
        path = Path(f'/proc/{self.process.pid}/task/{self.process.pid}/children')
        children = path.read_text().split()
        assert len(children) == 1, children
        os.kill(int(children[0]), signal.SIGKILL)

    def firmware(self):
        session = presented = 0
        audio = AudioClock()
        try:
            while not self.stop.is_set():
                self.put('HEARTBEAT', self.get('HEARTBEAT') + 1)
                request = self.get('SESSION_REQUEST')
                if request != session:
                    session = request
                    presented = 0
                    self.put('VIDEO_PRESENTED', 0)
                    self.put('AUDIO_READ', 0)
                    audio.reset()
                    self.put('SESSION_ACK', session)
                if session:
                    if not self.hold_audio:
                        self.put('AUDIO_READ', audio.sample(self.get('AUDIO_WRITE')))
                    publication = self.get('VIDEO_PUBLISH')
                    if not self.hold_video and publication & 2 and publication != presented:
                        base = OFFSETS['BUFFER1' if publication & 1 else 'BUFFER0']
                        pixel = base + (72 * 256 + 128) * 4
                        self.frames.append(bytes(self.shared[pixel:pixel+3]))
                        presented = publication
                        self.put('VIDEO_PRESENTED', publication)
                for at, action in self.actions:
                    if len(self.frames) >= at and at not in self.events:
                        self.events.add(at)
                        action()
                time.sleep(.0005)
        except BaseException as error:
            self.failure.append(error)

    def start(self, service, ticks, stdout, stderr):
        self.thread.start()
        self.process = subprocess.Popen([service, '--serve', str(self.saves), '--memory', str(self.memory),
                                         '--ticks', str(ticks)], stdout=stdout, stderr=stderr)
        return self.process

    def close(self):
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.stop.set()
        self.thread.join()
        self.shared.close()
        self.backing.close()
        assert not self.failure, self.failure

def recovery(service):
    with tempfile.TemporaryDirectory(prefix='tic80-recovery-') as directory:
        fixture = Fixture(Path(directory))
        fixture.actions = [
            (10, lambda: fixture.transfer(6, ordinary('old-worker', 2))),
            (40, lambda: fixture.transfer(10, cartridge('bad-worker', 'function TIC() while true do end end'))),
            (190, lambda: fixture.transfer(14, cartridge('late-worker', 'n=0\nfunction TIC() n=n+1; pmem(0,n); cls(5); '
                                                        'if n==3 then pmem(0,9999); while true do end end end'))),
            (230, lambda: fixture.transfer(18, ordinary('new-worker', 9))),
            (280, fixture.kill_child),
            (320, lambda: fixture.transfer(22, ordinary('new-worker', 9))),
        ]
        try:
            process = fixture.start(service, 390, subprocess.PIPE, subprocess.PIPE)
            output, errors = process.communicate(timeout=16)
            output, errors = output.decode(), errors.decode()
            assert process.returncode == 0, output + errors
            assert fixture.get('CART_ACK') == 22
            assert output.count('Cartridge loaded:') == 4, output
            assert errors.count('Cartridge execution timed out') == 2, errors
            assert errors.count('Cartridge failed') == 2, errors
            assert 'Cartridge execution process stopped' in errors
            # Rejected candidate resumes the original VM after its notice.
            assert fixture.frames[25] == fixture.frames[180]
            old = save_values(fixture.saves, 'old-worker')
            assert 50 <= old[0] <= 95 and old[1] == 1, old[:2]
            late = save_values(fixture.saves, 'late-worker')
            assert late[0] == 2, late[0]  # Partial hung-tick writes never reach saves.
            new = save_values(fixture.saves, 'new-worker')
            assert 100 <= new[0] <= 140 and new[1] == 2, new[:2]
            assert not list(fixture.saves.glob('*.tmp-*'))
            print('Service recovered from hung candidate, later hang and killed worker; saves remain coherent')
        finally:
            fixture.close()

def termination(service):
    with tempfile.TemporaryDirectory(prefix='tic80-stop-hang-') as directory:
        root = Path(directory)
        fixture = Fixture(root)
        fixture.actions = [
            (10, lambda: fixture.transfer(6, ordinary('stop-worker', 2))),
            (40, lambda: fixture.transfer(10, cartridge('stop-bad', "function TIC() trace('entered runaway callback'); while true do end end"))),
        ]
        try:
            with (root / 'out').open('w+b') as out, (root / 'err').open('w+b') as err:
                process = fixture.start(service, 1000, out, err)
                deadline = time.monotonic() + 6
                while b'entered runaway callback' not in (root / 'err').read_bytes():
                    assert process.poll() is None
                    assert time.monotonic() < deadline
                    time.sleep(.01)
                started = time.monotonic()
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=.75)
                assert process.returncode == 0, (root / 'err').read_text()
                elapsed = time.monotonic() - started
                old = save_values(fixture.saves, 'stop-worker')
                assert 25 <= old[0] <= 40 and old[1] == 1, old[:2]
                print(f'SIGTERM interrupts hung execution and flushes the previous game in {elapsed:.3f}s')
        finally:
            fixture.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--service', required=True)
    args = parser.parse_args()
    recovery(args.service)
    termination(args.service)
