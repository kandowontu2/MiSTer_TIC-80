"""Guarded animated raster captures; verifies exact pixels and frame integrity.

Pipeline captures establish image integrity, not connected-display latency.
Requires the matching installed player/RBF and finishes in MENU.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time
import paramiko
from PIL import Image
from hardware_ssh import command as ssh_command, connect, read_with_reconnect
from motion_fixture import cartridge, inspect_rgb
from monitor_journal import MonitorJournal

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', required=True)
parser.add_argument('--captures', type=int, default=16)
parser.add_argument('--remote-journal', action='store_true')
args = parser.parse_args()
assert 4 <= args.captures <= 100
folder = ROOT / 'build/motion'
folder.mkdir(parents=True, exist_ok=True)
client = paramiko.SSHClient()
client.load_system_host_keys()
connect(client,args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)


def command(text):
    return ssh_command(client, text)


def observer_read(text):
    # This callback is used only for journal/status reads, never core loads or
    # screenshot requests. The monitor keeps its original bounded lifetime.
    def reconnect():
        client.close()
        connect(client,args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
        print('Motion observer reconnected; monitor was not restarted', flush=True)
    return read_with_reconnect(lambda: command(text), reconnect,
                               retry_errors=(paramiko.SSHException, OSError, EOFError))


def selected():
    return command('cat /tmp/CORENAME').strip()


def guard():
    assert selected() in ('MENU', 'TIC-80'), 'Another core is selected'


def parent():
    return command('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()


def menu():
    guard()
    if selected() == 'TIC-80':
        command('test "$(cat /tmp/CORENAME)" = TIC-80 && printf "load_core /media/fat/menu.rbf\\n" > /dev/MiSTer_cmd')
    deadline = time.monotonic() + 20
    while selected() != 'MENU' or parent() or command('pidof tic80-vm || true').strip():
        guard()
        assert time.monotonic() < deadline, 'Service did not stop in MENU'
        time.sleep(.2)
    # A preceding CLI diagnostic may have stopped the service while its hook
    # was disabled. Let Frontier's one-second poll observe MENU before re-entry.
    time.sleep(2)
    assert selected() == 'MENU', 'Core changed during launcher rearm'


try:
    guard()
    hashes = {}
    for name, local, remote in [('player', 'build/arm/tic80-live', '/media/fat/games/TIC-80/TIC-80'),
                              ('rbf', 'build/fpga/output_files/TIC80.rbf', '/media/fat/_Other/TIC80_20260930.rbf'),
                              ('monitor', 'build/arm/tic80-runtime-monitor', '/tmp/tic80-mister-dev/runtime-monitor')]:
        hashes[name] = hashlib.sha256((ROOT / local).read_bytes()).hexdigest()
        assert command('sha256sum ' + remote).split()[0] == hashes[name]
    cart = folder / 'motion.tic'
    cart.write_bytes(cartridge())
    remote = '/tmp/tic80-mister-dev/motion'
    captures = []
    with client.open_sftp() as sftp:
        sftp.put(str(cart), remote + '.tic')
        with sftp.open(remote + '.mgl', 'w') as file:
            file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                       f' <file delay="3" type="f" index="0" path="{remote}.tic"/>\n'
                       '</mistergamedescription>\n')
        menu()
        command('test "$(cat /tmp/CORENAME)" = MENU && printf "load_core ' + remote + '.mgl\\n" > /dev/MiSTer_cmd')
        deadline = time.monotonic() + 35
        marker = f'Cartridge loaded: {cart.stat().st_size} bytes; reset=0'
        while True:
            guard()
            log = command('cat /media/fat/logs/TIC-80/tic80.log 2>/dev/null || true')
            if selected() == 'TIC-80' and parent().isdigit() and marker in log:
                break
            assert time.monotonic() < deadline, log
            time.sleep(.2)
        supervisor = parent()
        affinity = command('for p in ' + supervisor + ' $(pidof tic80-vm); do '
                           'printf "%s " "$p"; '
                           'sed -n "s/^Cpus_allowed_list:[[:space:]]*//p" /proc/$p/status; done').splitlines()
        assert len(affinity) == 2 and all(line.split()[1] == '0' for line in affinity), affinity
        time.sleep(1)
        # Observe the same session before and throughout screenshot activity.
        # A monitor started afterward cannot locate a previously recorded gap.
        if args.remote_journal:
            observer = MonitorJournal(command, 60, 13, read_command=observer_read)
            audio_rows = observer.rows()
            first_row = next(audio_rows)
            print('Motion monitor journal: ' + observer.directory, flush=True)
        else:
            _, audio_out, audio_err = client.exec_command(
                '/tmp/tic80-mister-dev/runtime-monitor --seconds 60 --interval-ms 13', timeout=45)
            first_audio = audio_out.readline()
            assert first_audio, 'Monitor did not produce a baseline'
        screenshots = '/media/fat/screenshots/TIC-80'
        for index in range(args.captures):
            assert selected() == 'TIC-80' and parent() == supervisor
            before = set(sftp.listdir(screenshots))
            command('test "$(cat /tmp/CORENAME)" = TIC-80 && printf "screenshot\\n" > /dev/MiSTer_cmd')
            capture = folder / f'capture-{index:03}.png'
            deadline = time.monotonic() + 10
            while True:
                assert selected() == 'TIC-80' and parent() == supervisor
                new = sorted(name for name in set(sftp.listdir(screenshots)) - before if name.endswith('.png'))
                if new:
                    sftp.get(screenshots + '/' + new[-1], str(capture))
                    if capture.read_bytes().endswith(b'\x00\x00\x00\x00IEND\xaeB\x60\x82'):
                        break
                assert time.monotonic() < deadline, 'Screenshot did not finish'
                time.sleep(.1)
            with Image.open(capture) as image:
                assert image.size == (256, 144)
                frame = inspect_rgb(image.convert('RGB').tobytes())
            if captures:
                assert 0 < (frame - captures[-1]['frame']) & 0xffffff < 600, 'Frame stopped or reset'
            captures.append(dict(frame=frame, capture=capture.name,
                                 sha256=hashlib.sha256(capture.read_bytes()).hexdigest()))
            (folder / 'progress.json').write_text(json.dumps(captures, indent=2) + '\n')
            print(f'Capture {index + 1}/{args.captures}: complete frame {frame}, all RGB bytes exact', flush=True)
            time.sleep(.137)
        if args.remote_journal:
            rows = [first_row] + list(audio_rows)
            samples = ''.join(json.dumps(row) + '\n' for row in rows)
        else:
            samples = first_audio + audio_out.read().decode()
            rows = [json.loads(line) for line in samples.splitlines()]
        (folder / 'audio.jsonl').write_text(samples)
        if not args.remote_journal:
            errors = audio_err.read().decode()
            assert audio_out.channel.recv_exit_status() == 0, errors
        print(f'Audio baseline={rows[0]["underruns"]}, final={rows[-1]["underruns"]}; '
              'observation includes all screenshot requests', flush=True)
        # Sampling can wake late while Main compresses screenshots. Check the
        # observed duration and bounded coverage, not an ideal wakeup count.
        assert len(rows) >= 240 and len({r['session'] for r in rows}) == 1
        assert rows[-1]['elapsed_ns'] - rows[0]['elapsed_ns'] >= 59_000_000_000
        assert max(b['elapsed_ns'] - a['elapsed_ns'] for a, b in zip(rows, rows[1:])) < 1_000_000_000
        assert all(r['underruns'] == 0 for r in rows)
        assert ((rows[-1]['played'] - rows[0]['played']) & 0xffffffff) > 450000
        assert selected() == 'TIC-80' and parent() == supervisor
        log = command('cat /media/fat/logs/TIC-80/tic80.log')
        assert all(word not in log for word in ('Cartridge failed', 'Cartridge rejected', 'Save error', 'TIC-80:', 'transport unavailable'))
        (folder / 'service.log').write_text(log)
        menu()
        result = dict(hashes=hashes, cartridge_sha256=hashlib.sha256(cart.read_bytes()).hexdigest(),
                      captures=captures, audio_samples=len(rows), underruns=0,
                      observed_seconds=(rows[-1]['elapsed_ns'] - rows[0]['elapsed_ns']) / 1e9,
                      maximum_sample_gap_ms=max(b['elapsed_ns'] - a['elapsed_ns']
                                               for a, b in zip(rows, rows[1:])) / 1e6,
                      playback_cpu_affinity=affinity,
                      remote_journal=observer.directory if args.remote_journal else None,
                      note='Pipeline integrity and playback; physical display cadence and latency are separate checks.')
        (folder / 'hardware-result.json').write_text(json.dumps(result, indent=2) + '\n')
        print('Animated hardware raster integrity and playback passed:', json.dumps(result), flush=True)
finally:
    client.close()
