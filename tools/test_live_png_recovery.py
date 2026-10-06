"""Guarded native PNG rejection/recovery test; requires TIC-80 selected.

Uses real MiSTer MGL transfers, a single supervised service and checked pmem.
MGL reloads the FPGA, so the service also resets its cached game during these
tests. tests/service_png_test.py separately verifies retaining the same VM.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
import uuid
import zlib
import paramiko
from hardware_ssh import connect

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from service_png_test import modern, padded
from service_recovery_test import ordinary

p = argparse.ArgumentParser()
p.add_argument('--host', required=True)
a = p.parse_args()
c = paramiko.SSHClient()
c.load_system_host_keys()
keys = ROOT / 'build/ssh_known_hosts'
if keys.exists():
    c.load_host_keys(str(keys))
connect(c,a.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)


def command(text):
    _, out, err = c.exec_command(text, timeout=15)
    output, errors = out.read().decode(), err.read().decode()
    if out.channel.recv_exit_status():
        raise RuntimeError(errors or output or text)
    return output


def log():
    return command('cat /media/fat/logs/TIC-80/tic80.log')


def supervisor():
    return command('for p in $(pidof TIC-80); do '
                   'test "$(cat /proc/$p/comm 2>/dev/null)" = "TIC-80" && printf "%s " "$p"; '
                   'done; true').strip()


def guard():
    if command('cat /tmp/CORENAME').strip() != 'TIC-80' or supervisor() != parent:
        raise RuntimeError('Core or service changed during the test')


def wait(predicate):
    deadline = time.monotonic() + 30
    while True:
        guard()
        current = log()
        if predicate(current):
            return
        if time.monotonic() > deadline:
            raise RuntimeError(current)
        time.sleep(.25)


def load(sftp, name, payload, extension='png'):
    base = '/tmp/tic80-mister-dev/png/recovery-' + name
    with sftp.open(base + '.' + extension, 'wb') as file:
        file.write(payload)
    with sftp.open(base + '.mgl', 'w') as file:
        file_index = 0x40 if extension == 'png' else 0
        file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                   f' <file delay="3" type="f" index="{file_index}" path="{base}.{extension}"/>\n'
                   '</mistergamedescription>\n')
    previous = log()
    guard()
    command('test "$(cat /tmp/CORENAME)" = "TIC-80" || exit 30; '
            f'printf "load_core {base}.mgl\\n" > /dev/MiSTer_cmd')
    return previous


def saved(sftp, key):
    path = '/media/fat/saves/TIC-80/' + hashlib.md5(key.encode()).hexdigest() + '.pmem'
    with sftp.open(path, 'rb') as file:
        data = file.read()
    assert len(data) == 1036 and data[:4] == b'TMPM'
    assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
    return struct.unpack_from('<256I', data, 12)


try:
    expected = hashlib.sha256((ROOT / 'build/arm/tic80-live').read_bytes()).hexdigest()
    if command('sha256sum /media/fat/games/TIC-80/TIC-80').split()[0] != expected:
        raise RuntimeError('Installed player differs from the tested build')
    parent = supervisor()
    assert parent and ' ' not in parent
    guard()
    key = 'tic80-png-recovery-' + uuid.uuid4().hex
    native = ordinary(key, 2)
    good = modern(native)
    with c.open_sftp() as sftp:
        previous = load(sftp, 'good', good)
        marker = f'Cartridge loaded: {len(good)} bytes; reset=0'
        wait(lambda text: text.count(marker) > previous.count(marker))
        time.sleep(1.5)
        before = saved(sftp, key)
        assert before[0] >= 60 and before[1] == 1, before[:2]
        records = []
        for name in ('corrupt-image', 'oversize-native'):
            payload = (ROOT / 'build/png-carts' / (name + '.png')).read_bytes()
            previous = load(sftp, name, payload)
            wait(lambda text: text.count('Cartridge rejected:') > previous.count('Cartridge rejected:'))
            time.sleep(3.5)
            guard()
            after = saved(sftp, key)
            assert after[0] > before[0] and after[1] > before[1], (before[:2], after[:2])
            records.append(dict(rejected=name, resumed_ticks=after[0], boots=after[1]))
            before = after
            print(name + ': rejected; cached PNG game resumed and saved', flush=True)
        previous = load(sftp, 'native-same-save', native, 'tic')
        marker = f'Cartridge loaded: {len(native)} bytes; reset=0'
        wait(lambda text: text.count(marker) > previous.count(marker))
        time.sleep(1.5)
        guard()
        final = saved(sftp, key)
        assert final[0] > before[0] and final[1] > before[1], (before[:2], final[:2])
        maximum = modern(padded(ordinary(key + '-max', 9), 4*1024*1024))
        previous = load(sftp, 'maximum-native', maximum)
        marker = f'Cartridge loaded: {len(maximum)} bytes; reset=0'
        wait(lambda text: text.count(marker) > previous.count(marker))
        time.sleep(1.5)
        guard()
        boundary = saved(sftp, key + '-max')
        assert boundary[0] >= 60 and boundary[1] == 1, boundary[:2]
        result = dict(player_sha256=expected, supervisor_pid=parent, rejections=records,
                      native_shared_save_ticks=final[0], native_shared_save_boots=final[1],
                      maximum_extracted_bytes=4*1024*1024, maximum_cart_ticks=boundary[0])
        (ROOT / 'build/live-png-recovery.json').write_text(json.dumps(result, indent=2) + '\n')
        print('Native load after failed PNGs retained the shared save: ' + json.dumps(result), flush=True)
finally:
    c.close()
