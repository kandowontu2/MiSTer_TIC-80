"""Load a five-second native clock cartridge on the installed TIC-80 core.

Requires a matching ARM player, trusted SSH host key and TM_SSH_PASSWORD.
Leaves TIC-80 on its splash screen after the cartridge requests exit.
--format png wraps the same fixture in an upstream-compatible PNG container.
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
parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
parser.add_argument('--format', choices=('native', 'png'), default='native')
args = parser.parse_args()
client = paramiko.SSHClient()
client.load_system_host_keys()
keys = ROOT / 'build/ssh_known_hosts'
if keys.exists():
    client.load_host_keys(str(keys))
connect(client,args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)

def command(text):
    _, out, err = client.exec_command(text, timeout=15)
    output, errors = out.read().decode(), err.read().decode()
    if out.channel.recv_exit_status():
        raise RuntimeError(errors or output or text)
    return output

try:
    if command('cat /tmp/CORENAME').strip() != 'TIC-80':
        raise RuntimeError('TIC-80 must already be selected')
    expected = hashlib.sha256((ROOT / 'build/arm/tic80-live').read_bytes()).hexdigest()
    if command('sha256sum /media/fat/games/TIC-80/TIC-80').split()[0] != expected:
        raise RuntimeError('Installed player does not match the tested ARM build')
    saveid = 'tic80-clock-' + uuid.uuid4().hex
    marker = saveid + ' finished'
    code = (f'-- script: lua\n-- saveid: {saveid}\n'
            'previous=nil\n'
            'function BOOT() pmem(1,pmem(1)+1); pmem(2,math.floor(time())) end\n'
            'function TIC() local t=math.floor(time()); pmem(0,t); pmem(3,pmem(3)+1); '
            'if previous then pmem(4,math.max(pmem(4),t-previous)) end; previous=t; '
            "cls(0); print('Clock regression',28,36,12,false,2); print(t,100,72,11); "
            f"if t>=5000 then trace('{marker}'); exit() end end\n").encode()
    cartridge = bytes([17, 0, 0, 0, 5, len(code) & 255, len(code) >> 8, 0]) + code
    suffix = '' if args.format == 'native' else '-png'
    extension = '.tic' if args.format == 'native' else '.png'
    if args.format == 'png':
        sys.path.insert(0, str(ROOT / 'tests'))
        from service_png_test import modern
        cartridge = modern(cartridge)
    remote = '/tmp/tic80-mister-dev/clock' + suffix
    with client.open_sftp() as sftp:
        with sftp.open(remote + extension, 'wb') as file:
            file.write(cartridge)
        with sftp.open(remote + '.mgl', 'w') as file:
            file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                       f' <file delay="3" type="f" index="0" path="{remote}{extension}"/>\n'
                       '</mistergamedescription>\n')
        command('test "$(cat /tmp/CORENAME)" = "TIC-80" || exit 30; '
                f'printf "load_core {remote}.mgl\\n" > /dev/MiSTer_cmd')
        deadline = time.monotonic() + 35
        while True:
            if command('cat /tmp/CORENAME').strip() != 'TIC-80':
                raise RuntimeError('Another core selected during clock test')
            log = command('cat /media/fat/logs/TIC-80/tic80.log')
            if marker in log and 'Cartridge requested exit' in log:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('Clock cartridge did not finish: ' + log)
            time.sleep(.25)
        key = hashlib.md5(saveid.encode()).hexdigest()
        local = ROOT / 'build' / f'hardware-clock{suffix}.pmem'
        # The service logs the exit before flushing its final save.
        while True:
            try:
                sftp.get('/media/fat/saves/TIC-80/' + key + '.pmem', str(local))
                data = local.read_bytes()
                if len(data) == 1036 and struct.unpack_from('<I', data, 12)[0] >= 5000:
                    break
            except FileNotFoundError:
                pass
            if time.monotonic() >= deadline:
                raise RuntimeError('Final clock save did not arrive')
            time.sleep(.1)
    assert data[:4] == b'TMPM' and struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
    elapsed, boots, boot_time, ticks, largest_gap = struct.unpack_from('<5I', data, 12)
    assert 5000 <= elapsed < 5500, elapsed
    assert boots == 1 and boot_time < 10, (boots, boot_time)
    assert 220 <= ticks <= 380, ticks
    assert largest_gap < 250, largest_gap
    result = dict(player_sha256=expected, elapsed_ms=elapsed, boots=boots,
                  boot_time_ms=boot_time, ticks=ticks, largest_gap_ms=largest_gap)
    (ROOT / 'build' / f'live-clock{suffix}.json').write_text(json.dumps(result, indent=2) + '\n')
    print(args.format + ' live clock and exit/save passed: ' + json.dumps(result))
finally:
    client.close()
