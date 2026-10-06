"""Exercise the real cartridge service against a bounded shared DDR fixture."""
import argparse
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from audio_fixture import AudioClock


def cartridge(saveid, color, fft=False):
    code = (f"-- script: lua\n-- saveid: {saveid}\n"
            "function BOOT() pmem(1,pmem(1)+1); local x,y=mouse(); pmem(2,x); pmem(3,y) end\n"
            "previous=nil\n"
            f"function TIC() pmem(0,pmem(0)+1); cls({color}); local t=math.floor(time()); pmem(4,t); "
            "if previous then pmem(5,math.max(pmem(5),t-previous)) end; previous=t "
            + ("pmem(7,math.floor(fftr(32)+0.5)) " if fft else "") + "end\n").encode()
    return bytes([17, 0, 0, 0, 5, len(code) & 255, len(code) >> 8, 0]) + code


def rejected_cartridge():
    # Structurally valid: the candidate opens its microphone, then fails TIC.
    code=b"-- script: lua\nfunction TIC() error('FFT candidate rejected') end\n"
    return bytes([17,0,0,0,5,len(code),0,0])+code


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--service', required=True)
    parser.add_argument('--fft-fixture', action='store_true')
    args = parser.parse_args()
    protocol = json.loads((Path(__file__).resolve().parents[1] / 'protocol/memory_map.json').read_text())
    offsets = protocol['offsets']
    with tempfile.TemporaryDirectory(prefix='tic80-service-') as directory:
        root = Path(directory)
        memory = root / 'ddr'
        saves = root / 'saves'
        saves.mkdir()
        with memory.open('w+b') as backing:
            backing.truncate(protocol['region_bytes'])
            with mmap.mmap(backing.fileno(), protocol['region_bytes']) as shared:
                def get(name):
                    return struct.unpack_from('<I', shared, offsets[name])[0]

                def put(name, value):
                    ctypes.c_uint32.from_buffer(shared, offsets[name]).value = value & 0xffffffff

                put('IDENTITY', protocol['magic'])
                put('GEOMETRY', protocol['width'] | protocol['height'] << 16)
                put('KEYBOARD', 2)
                put('MOUSE', 120 | 68 << 8)
                stop = threading.Event()
                frames = []
                failures = []

                def firmware():
                    session = presented = blocked_nonce = 0
                    audio = AudioClock()
                    events = set()
                    while not stop.is_set():
                        put('HEARTBEAT', get('HEARTBEAT') + 1)
                        request = get('SESSION_REQUEST')
                        if request != session and request != blocked_nonce:
                            session = request
                            presented = 0
                            put('VIDEO_PRESENTED', 0)
                            put('AUDIO_READ', 0)
                            audio.reset()
                            put('SESSION_ACK', session)
                        if session:
                            put('AUDIO_READ', audio.sample(get('AUDIO_WRITE')))
                            publication = get('VIDEO_PUBLISH')
                            if publication & 2 and publication != presented:
                                base = offsets['BUFFER1' if publication & 1 else 'BUFFER0']
                                frames.append(bytes(shared[base+(72*256+128)*4:base+(72*256+128)*4+3]))
                                presented = publication
                                put('VIDEO_PRESENTED', publication)
                        count = len(frames)
                        for at, ticket, payload in [(10, 6, cartridge('service-a', 2,args.fft_fixture)),
                                                    (95, 10, rejected_cartridge() if args.fft_fixture else b'bad'),
                                                    (340, 14, cartridge('service-b', 9,args.fft_fixture))]:
                            if count >= at and at not in events:
                                if ticket != 6 and get('CART_ACK') != ticket - 4:
                                    failures.append('Previous cartridge was not acknowledged')
                                events.add(at)
                                shared[offsets['CART_DATA']:offsets['CART_DATA']+len(payload)] = payload
                                struct.pack_into('<I', shared, offsets['CART_META']+4, len(payload))
                                put('CART_META', ticket)
                        if count >= 260 and 260 not in events:
                            events.add(260)
                            put('STATUS', 1)
                        if count >= 265:
                            put('STATUS', 0)
                        if count >= 365 and 365 not in events:
                            events.add(365)
                            # FPGA reload: flush state, ignore the stale ARM
                            # nonce until it changes, and retain staging ACK.
                            blocked_nonce = session
                            session = presented = 0
                            put('SESSION_ACK', 0)
                            put('VIDEO_PRESENTED', 0)
                            put('AUDIO_READ', 0)
                        time.sleep(.0005)

                thread = threading.Thread(target=firmware)
                thread.start()
                try:
                    command=[args.service, '--serve', str(saves), '--memory', str(memory),'--ticks','420']
                    env=None
                    if args.fft_fixture:
                        command+=['--fft-device','Mic A']
                        env=dict(os.environ,TM_TEST_FFT_LOG=str(root/'capture-events'))
                    result = subprocess.run(command,env=env,capture_output=True,text=True,timeout=20)
                finally:
                    stop.set()
                    thread.join()
                assert result.returncode == 0, result.stdout + result.stderr
                assert not failures, failures
                assert get('CART_ACK') == 14
                assert result.stdout.count('Cartridge loaded:') == 2, result.stdout
                assert result.stdout.count('Cartridge restarted:') == 2, result.stdout
                # Invalid input displays a notice, then resumes the same VM.
                red = frames[40]
                assert frames[240] == red and frames[280] == red
                assert frames[380] != red
                for key, boots, lower, upper in [('service-a', 2, 180, 250), ('service-b', 2, 70, 95)]:
                    path = saves / (hashlib.md5(key.encode()).hexdigest() + '.pmem')
                    data = path.read_bytes()
                    assert len(data) == 1036 and data[:4] == b'TMPM'
                    assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
                    ticks, actual_boots = struct.unpack_from('<II', data, 12)
                    assert lower <= ticks <= upper, (key, ticks)
                    assert actual_boots == boots, (key, actual_boots)
                    assert struct.unpack_from('<II', data, 20) == (120, 68), 'BOOT must receive the actual input snapshot'
                    elapsed, largest_gap = struct.unpack_from('<II', data, 28)
                    assert 0 < elapsed < 5000, (key, 'VM elapsed milliseconds', elapsed, result.stdout, result.stderr)
                    assert largest_gap < 500, (key, 'clock advanced during invalid-load notice', largest_gap)
                    if args.fft_fixture: assert struct.unpack_from('<I',data,12+7*4)[0]==512,(key,'capture did not resume',result.stderr)
                assert not list(saves.glob('*.tmp-*'))
                if args.fft_fixture:
                    assert result.stderr.count('Cartridge microphone capture: active')==5,result.stderr
                    assert result.stderr.count('Previous cartridge microphone capture: active')==1,result.stderr
                    assert 'FFT candidate rejected' in result.stderr and 'script_error=1' in result.stderr,result.stderr
                    assert 'microphone capture: unavailable' not in result.stderr,result.stderr
                    events=(root/'capture-events').read_text().splitlines()
                    assert sum(' device-open ' in line for line in events)==sum(' device-close ' in line for line in events),events
                    assert not any(' device-busy ' in line for line in events),events
                    print('Service capture: exclusive microphone handoff, rejected-cart resume, reset and FPGA reload preserved nonzero FFT data')
                print('Service: valid load, invalid-load recovery with paused clock, reset, hot-swap, FPGA reload, background saves and graceful exit passed')


if __name__ == '__main__':
    main()
