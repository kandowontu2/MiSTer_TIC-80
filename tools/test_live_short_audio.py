"""Verify finite 800/1600/2400-frame PCM streams drain on the real FPGA.

Requires TIC-80 selected and matching installed artifacts. Temporarily moves
its own launch hook aside, gracefully stops its supervisor, then runs bounded
CLI clips. Restores the unchanged hook in all cases.
Leaves TIC-80 without a service;
select MENU and TIC-80 afterward to launch a fresh service through Frontier.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import time
import paramiko
from hardware_ssh import command as ssh_command, connect

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--host', required=True)
a = p.parse_args()
c = paramiko.SSHClient()
c.load_system_host_keys()
if (ROOT / 'build/ssh_known_hosts').exists():
    c.load_host_keys(str(ROOT / 'build/ssh_known_hosts'))
connect(c,a.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)


def command(text):
    return ssh_command(c, text)


def guard():
    if command('cat /tmp/CORENAME').strip() != 'TIC-80':
        raise RuntimeError('Another core selected')


def supervisor():
    return command('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()


hook = '/media/fat/games/TIC-80/_handler.sh'
disabled_hook = hook + '.diagnostic-disabled'
hook_moved = False
try:
    guard()
    for local, remote in [('build/arm/tic80-live', '/media/fat/games/TIC-80/TIC-80'),
                          ('build/fpga/output_files/TIC80.rbf', '/media/fat/_Other/TIC80_20260930.rbf')]:
        assert command('sha256sum ' + remote).split()[0] == hashlib.sha256((ROOT / local).read_bytes()).hexdigest()
    parent = supervisor()
    assert parent.isdigit(), 'Expected exactly one TIC-80 supervisor'
    birth = command(f'awk \'{{print $22}}\' /proc/{parent}/stat').strip()
    assert birth.isdigit()
    assert command('sha256sum ' + hook).split()[0] == hashlib.sha256((ROOT / 'games/TIC-80/_handler.sh').read_bytes()).hexdigest()
    guard()
    command('test ! -e ' + disabled_hook)
    hook_moved = True # Restore even if SSH fails after the atomic rename.
    command('test "$(cat /tmp/CORENAME)" = TIC-80 && mv ' + hook + ' ' + disabled_hook)
    command(f'test "$(cat /tmp/CORENAME)" = TIC-80 && '
            f'test "$(cat /proc/{parent}/comm)" = TIC-80 && '
            f'test "$(awk \'{{print $22}}\' /proc/{parent}/stat)" = {birth} && kill -TERM {parent}')
    deadline = time.monotonic() + 10
    while supervisor() or command('pidof tic80-vm || true').strip():
        guard()
        if time.monotonic() >= deadline:
            raise RuntimeError('Supervisor/worker did not stop gracefully')
        time.sleep(.1)
    remote = '/tmp/tic80-mister-dev/short-audio.tic'
    local = ROOT / 'build/soak/music-soak.tic'
    with c.open_sftp() as sftp:
        sftp.put(str(local), remote)
    assert command('sha256sum ' + remote).split()[0] == hashlib.sha256(local.read_bytes()).hexdigest()
    results = []
    for ticks in (1, 2, 3):
        guard()
        assert not supervisor()
        output = command(f'test "$(cat /tmp/CORENAME)" = TIC-80 || exit 30; '
                         f'/media/fat/games/TIC-80/TIC-80 {remote} {ticks}')
        match = re.search(r'Live frames acknowledged=(\d+) stereo_frames_played=(\d+) wall_seconds=([\d.]+)', output)
        assert match, output
        frames, played, seconds = int(match[1]), int(match[2]), float(match[3])
        assert frames == ticks and played == ticks*800 and seconds < .5, output
        guard()
        assert not supervisor() and not command('pidof tic80-vm || true').strip()
        results.append(dict(ticks=ticks, played_sample_frames=played, seconds=seconds))
        print(output.strip(), flush=True)
    (ROOT / 'build/short-audio-hardware.json').write_text(json.dumps(results, indent=2) + '\n')
    print('Short-clip fallback and two-tick startup threshold drain every published sample on hardware')
finally:
    if hook_moved:
        command(f'if test -e {disabled_hook}; then test ! -e {hook} && mv {disabled_hook} {hook}; fi')
    c.close()
