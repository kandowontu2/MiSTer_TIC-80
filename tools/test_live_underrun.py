"""Verify hardware underrun accounting with a bounded pause of our supervisor."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import paramiko
from hardware_ssh import connect

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
    _, out, err = c.exec_command(text, timeout=10)
    output, errors = out.read().decode(), err.read().decode()
    if out.channel.recv_exit_status():
        raise RuntimeError(errors or output or text)
    return output


def supervisor():
    return command('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()


try:
    expected = hashlib.sha256((ROOT / 'build/arm/tic80-live').read_bytes()).hexdigest()
    if command('cat /tmp/CORENAME').strip() != 'TIC-80' or command('sha256sum /media/fat/games/TIC-80/TIC-80').split()[0] != expected:
        raise RuntimeError('Matching TIC-80 must be selected')
    parent = supervisor()
    assert parent.isdigit()
    birth = command(f'awk \'{{print $22}}\' /proc/{parent}/stat').strip()
    assert birth.isdigit()
    owned = f'test "$(cat /proc/{parent}/comm 2>/dev/null)" = TIC-80 && test "$(awk \'{{print $22}}\' /proc/{parent}/stat)" = {birth}'
    _, out, err = c.exec_command('/tmp/tic80-mister-dev/runtime-monitor --seconds 4 --interval-ms 100', timeout=10)
    first = json.loads(out.readline())
    # Keep the pause/resume on the board: network round trips must not lengthen
    # the pause. The shell trap resumes our exact process if the command exits.
    command(f'set -e; test "$(cat /tmp/CORENAME)" = TIC-80; {owned} || exit 1; '
            f'resume() {{ {owned} && kill -CONT {parent}; }}; '
            f'trap resume EXIT HUP INT TERM; '
            f'test "$(cat /tmp/CORENAME)" = TIC-80; {owned} || exit 1; '
            f'kill -STOP {parent}; sleep 0.35; '
            f'{owned} || exit 1; kill -CONT {parent}; trap - EXIT HUP INT TERM')
    rows = [first] + [json.loads(line) for line in out.read().decode().splitlines()]
    errors = err.read().decode()
    assert out.channel.recv_exit_status() == 0, errors
    assert supervisor() == parent and command('cat /tmp/CORENAME').strip() == 'TIC-80'
    gaps = (rows[-1]['underruns'] - first['underruns']) & 0xffffffff
    assert 8000 < gaps < 20000, gaps
    assert len({r['session'] for r in rows}) == 1
    assert all(abs(((r['slots']-r['underruns']) & 0xffffffff)-r['played']) <= 2 for r in rows)
    assert rows[-1]['played'] > rows[-10]['played']
    result = dict(supervisor_pid=parent, forced_pause_seconds=.35, underrun_slots=gaps,
                  equivalent_silence_ms=gaps/48, samples=len(rows))
    (ROOT / 'build/stats-underrun-proof.json').write_text(json.dumps(result, indent=2) + '\n')
    (ROOT / 'build/stats-underrun-proof.jsonl').write_text('\n'.join(json.dumps(row) for row in rows) + '\n')
    print('Hardware pause/underrun/resume accounting passed: ' + json.dumps(result))
finally:
    c.close()
