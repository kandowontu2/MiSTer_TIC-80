"""Guarded native MiSTer test of failed candidates and hung-game recovery.

Requires matching installed ARM player/RBF and TIC-80 already selected.
Leaves the recovered test cartridge running; no controller events are sent.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import uuid
import zlib
import paramiko
from hardware_ssh import connect

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
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

def log():
    return command('cat /media/fat/logs/TIC-80/tic80.log')

def supervisor():
    # BusyBox pidof also matches the shared executable of tic80-vm children.
    return command('for p in $(pidof TIC-80); do '
                   'test "$(cat /proc/$p/comm 2>/dev/null)" = "TIC-80" && printf "%s " "$p"; '
                   'done; true').strip()

def guard():
    if command('cat /tmp/CORENAME').strip() != 'TIC-80':
        raise RuntimeError('Another core is selected')
    if supervisor() != parent:
        raise RuntimeError('The service process changed during recovery testing')

def wait(check, description):
    deadline = time.monotonic() + 40
    while True:
        guard()
        current = log()
        if check(current):
            return current
        if time.monotonic() >= deadline:
            raise RuntimeError(description + ': ' + current)
        time.sleep(.25)

def load(sftp, name, body):
    saveid = 'tic80-fault-' + run + '-' + name
    code = (f'-- script: lua\n-- saveid: {saveid}\n' + body).encode()
    payload = bytes([17, 0, 0, 0, 5, len(code) & 255, len(code) >> 8, 0]) + code
    remote = '/tmp/tic80-mister-dev/fault-' + name
    with sftp.open(remote + '.tic', 'wb') as file:
        file.write(payload)
    with sftp.open(remote + '.mgl', 'w') as file:
        file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                   f' <file delay="3" type="f" index="0" path="{remote}.tic"/>\n'
                   '</mistergamedescription>\n')
    previous = log()
    guard()
    command('test "$(cat /tmp/CORENAME)" = "TIC-80" || exit 30; '
            f'printf "load_core {remote}.mgl\\n" > /dev/MiSTer_cmd')
    return saveid, len(payload), previous

def saved(sftp, saveid):
    remote = '/media/fat/saves/TIC-80/' + hashlib.md5(saveid.encode()).hexdigest() + '.pmem'
    with sftp.open(remote, 'rb') as file:
        data = file.read()
    assert len(data) == 1036 and data[:4] == b'TMPM'
    assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
    return struct.unpack_from('<256I', data, 12)

try:
    expected = hashlib.sha256((ROOT / 'build/arm/tic80-live').read_bytes()).hexdigest()
    if command('sha256sum /media/fat/games/TIC-80/TIC-80').split()[0] != expected:
        raise RuntimeError('Installed player does not match this build')
    parent = supervisor()
    if not parent or ' ' in parent:
        raise RuntimeError('Expected one service supervisor')
    guard()
    run = uuid.uuid4().hex
    with client.open_sftp() as sftp:
        good, size, previous = load(sftp, 'good', 'function BOOT() pmem(1,pmem(1)+1) end\n'
                                  'function TIC() pmem(0,pmem(0)+1); cls(2) end\n')
        marker = f'Cartridge loaded: {size} bytes; reset=0'
        wait(lambda text: text.count(marker) > previous.count(marker), 'Initial good cartridge did not load')
        time.sleep(1.5)
        before = saved(sftp, good)
        _, _, previous = load(sftp, 'candidate', 'function TIC() while true do end end\n')
        wait(lambda text: text.count('Cartridge rejected:') > previous.count('Cartridge rejected:') and
             text.count('Cartridge execution timed out') > previous.count('Cartridge execution timed out'),
             'Hung candidate was not rejected')
        time.sleep(3.5)  # Two-second notice, then resumed autosave.
        guard()
        resumed = saved(sftp, good)
        assert resumed[0] > before[0] and resumed[1] > before[1], (before[:2], resumed[:2])
        print('Native hung-candidate rejection and cached game recovery passed', flush=True)
        late, size, previous = load(sftp, 'late', 'n=0\nfunction TIC() n=n+1; pmem(0,n); cls(5); '
                                    'if n==3 then pmem(0,9999); while true do end end end\n')
        wait(lambda text: text.count('Cartridge failed') > previous.count('Cartridge failed'),
             'Later game hang did not return to error screen')
        time.sleep(.3)
        guard()
        snapshot = saved(sftp, late)
        assert snapshot[0] == 2, snapshot[0]
        assert not command('pidof tic80-vm || true').strip(), 'Hung worker was left running'
        print('Native later-hang recovery saved the last completed tick and reaped the VM', flush=True)
        recovered, size, previous = load(sftp, 'recovered', 'function BOOT() pmem(1,pmem(1)+1) end\n'
                                       'function TIC() pmem(0,pmem(0)+1); cls(9) end\n')
        marker = f'Cartridge loaded: {size} bytes; reset=0'
        wait(lambda text: text.count(marker) > previous.count(marker), 'Recovery cartridge did not load')
        time.sleep(1.5)
        guard()
        final = saved(sftp, recovered)
        assert final[0] >= 60 and final[1] == 1, final[:2]
        result = dict(player_sha256=expected, supervisor_pid=parent, before_ticks=before[0],
                      resumed_ticks=resumed[0], hung_game_last_complete_tick=snapshot[0], recovered_ticks=final[0])
        (ROOT / 'build/live-recovery.json').write_text(json.dumps(result, indent=2) + '\n')
        print('Native new cartridge after a hang passed: ' + json.dumps(result), flush=True)
finally:
    client.close()
