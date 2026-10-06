"""Read-only HDMI clock/geometry gate on the selected TIC-80 build.

Checks ADV7513 automatic CTS against standard 720p/60. Captures and PLL lock
cannot substitute for this clock measurement or the connected panel check.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import paramiko
from analyze_hdmi_clock import analyze
from hardware_ssh import command, connect

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', required=True)
parser.add_argument('--rbf', type=Path, default=root / 'build/fpga/output_files/TIC80.rbf')
parser.add_argument('--output', type=Path, default=root / 'build/hdmi-hardware-result.json')
args = parser.parse_args()
client = paramiko.SSHClient()
client.load_system_host_keys()
connect(client,args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
result = {}
try:
    assert command(client, 'cat /tmp/CORENAME').strip() == 'TIC-80'
    hashes = {}
    for label, local, remote in [
        ('rbf', args.rbf, '/media/fat/_Other/TIC80_20260930.rbf'),
        ('player', root / 'build/arm/tic80-live', '/media/fat/games/TIC-80/TIC-80')]:
        hashes[label] = hashlib.sha256(local.read_bytes()).hexdigest()
        assert command(client, 'sha256sum ' + remote).split()[0] == hashes[label]
    handles = command(client, 'for p in $(pidof MiSTer); do ls -l /proc/$p/fd | grep i2c || true; done')
    assert '/dev/i2c-1' in handles, 'Expected transmitter bus is not open in Main'
    registers = '0x01 0x02 0x03 0x04 0x05 0x06 0x0a 0x3e 0x3f 0x42 0x55 0x9d 0x9e'.split()
    raw = command(client, 'for n in 1 2 3 4 5 6 7 8; do '
                  'test "$(cat /tmp/CORENAME)" = TIC-80 || exit 31; '
                  'for r in ' + ' '.join(registers) + '; do '
                  'printf "%s " "$r"; i2cget -y 1 0x39 "$r" b || exit 32; '
                  'done; sleep 0.25; done')
    lines = raw.splitlines()
    assert len(lines) == len(registers) * 8
    rows = [dict(registers=dict(line.split() for line in lines[start:start + len(registers)]))
            for start in range(0, len(lines), len(registers))]
    assert command(client, 'cat /tmp/CORENAME').strip() == 'TIC-80'
    assert command(client, 'sha256sum /media/fat/_Other/TIC80_20260930.rbf').split()[0] == hashes['rbf']
    result = dict(hashes=hashes, rows=rows, passed=False,
                  note='CTS-derived pixel clock uses nominal 48 kHz audio; panel acceptance remains a human check.')
    result['analysis'] = analyze(rows)
    result['passed'] = True
    print('HDMI clock gate passed:', json.dumps(result['analysis']), flush=True)
finally:
    if result:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    client.close()
