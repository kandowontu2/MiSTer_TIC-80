"""Observe a fresh Tetris launch after the controller diagnostic."""
import argparse
import json
import os
from pathlib import Path
import time
import paramiko
from hardware_ssh import command, connect

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', choices=('controller-keys', 'controller-qe'), required=True)
args = parser.parse_args()
prefix = args.prefix
installed = json.loads((root/'build'/(prefix+'-installed.json')).read_text())
client = paramiko.SSHClient()
client.load_system_host_keys()
connect(client,'192.168.1.176', username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
def selected():
    assert command(client, 'cat /tmp/CORENAME').strip() == 'TIC-80', 'Another core selected'
try:
    selected()
    for path, sha in installed['hashes'].items():
        assert command(client, 'sha256sum '+path).split()[0] == sha, path
    before = command(client, 'cat /media/fat/logs/TIC-80/tic80.log')
    marker = 'Cartridge loaded: 25147 bytes; reset=0'
    command(client, 'test "$(cat /tmp/CORENAME)" = TIC-80 && '
            'printf "load_core /tmp/tic80-mister-dev/controller-keys/tetris.mgl\\n" > /dev/MiSTer_cmd')
    deadline = time.monotonic()+40
    while True:
        selected()
        log = command(client, 'cat /media/fat/logs/TIC-80/tic80.log')
        if log.count(marker) > before.count(marker): break
        assert time.monotonic() < deadline, 'Fresh Tetris launch did not complete'
        time.sleep(.25)
    time.sleep(2)
    selected()
    journal = prefix+'-tetris-fresh.jsonl'
    raw = command(client, '/tmp/tic80-mister-dev/runtime-monitor --seconds 10 --interval-ms 13')
    (root/'build'/journal).write_text(raw)
    rows = [json.loads(line) for line in raw.splitlines()]
    result = dict(hashes=installed['hashes'], journal=journal, samples=len(rows), passed=False)
    try:
        assert len(rows) > 500
        first, last = rows[0], rows[-1]
        result.update(first=first, last=last, underruns=max(row['underruns'] for row in rows))
        assert len({row['session'] for row in rows}) == 1
        assert result['underruns'] == 0, 'Audio underruns; raw journal retained'
        assert last['elapsed_ns'] >= 10_000_000_000
        for field in ('slots', 'written', 'played', 'publication', 'presented', 'heartbeat'):
            assert 0 < ((last[field]-first[field]) & 0xffffffff) < 0x80000000, field
        selected()
        result['passed'] = True
    finally:
        (root/'build'/(prefix+'-tetris.json')).write_text(json.dumps(result, indent=2)+'\n')
    print('Fresh Tetris: advancing video/audio, coherent session and zero underruns')
finally:
    client.close()
