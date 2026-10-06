"""Guarded music/clock/pmem soak using the read-only hardware monitor.

Requires a generated build/soak/music-soak.tic, matching installed RBF/player,
and TIC-80 selected. Never switches away from another core. After collecting
the full run, switches to MENU to flush the final save, then evaluates its
acceptance checks. Use a demo afterward for normal play.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import time
import zlib
import paramiko
from analyze_audio_queue import analyze
from analyze_video_clock import analyze as analyze_video
from hardware_process import descriptors
from hardware_ssh import command as ssh_command, connect, read_with_reconnect
from monitor_journal import MonitorJournal

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--host', required=True)
p.add_argument('--seconds', type=int, default=600)
p.add_argument('--synchronized-video', action='store_true',
               help='Require the carrier to share playback timing: 800 sample slots per raster')
p.add_argument('--remote-journal', action='store_true',
               help='Journal once on the board; reconnect only read-only observation after SSH loss')
p.add_argument('--interval-ms', type=int, default=197,
               help='197 ms samples all tick phases; multiples of 60 Hz can alias queue drift')
a = p.parse_args()
assert 10 <= a.seconds <= 3600
assert 10 <= a.interval_ms <= 250
c = paramiko.SSHClient()
c.load_system_host_keys()
if (ROOT / 'build/ssh_known_hosts').exists():
    c.load_host_keys(str(ROOT / 'build/ssh_known_hosts'))
connect(c,a.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
rows, saves, memory = [], [], []
observer_reconnects = 0
folder = ROOT / 'build/soak'


def command(text):
    # Core selection and monitor launch are dispatched once, even on SSH loss.
    return ssh_command(c,text)


def read_observation(read):
    if not a.remote_journal:
        return read()
    return read_with_reconnect(read, reconnect_observer,
                               retry_errors=(paramiko.SSHException, OSError, EOFError))


def observe(text):
    return read_observation(lambda: ssh_command(c,text))


def reconnect_observer():
    global observer_reconnects
    c.close()
    connect(c,a.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
    observer_reconnects += 1
    print('SSH observer reconnected; the bounded monitor was not restarted', flush=True)


def supervisor():
    return observe('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()


def guard():
    if observe('cat /tmp/CORENAME').strip() != 'TIC-80' or supervisor() != parent:
        raise RuntimeError('Core or supervisor changed during soak')


def saved(sftp=None):
    if sftp is None:
        def read_save():
            with c.open_sftp() as current:
                return saved(current)
        return read_observation(read_save)
    with sftp.open(savepath, 'rb') as file:
        data = file.read()
    assert len(data) == 1036 and data[:4] == b'TMPM'
    assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
    ticks, elapsed, boots, largest_gap = struct.unpack_from('<4I', data, 12)
    assert boots == 1, boots
    return dict(ticks=ticks, elapsed_ms=elapsed, boots=boots, largest_gap_ms=largest_gap)


def process_memory(elapsed):
    children = observe('ps -eo pid,ppid,comm | '
                       f'awk \'$2 == {parent} && $3 == "tic80-vm" {{print $1}}\'').split()
    assert len(children) == 1 and children[0].isdigit(), children
    sample = dict(seconds=elapsed, parent=int(parent), worker=int(children[0]))
    for name, pid in [('parent', parent), ('worker', children[0])]:
        status = observe(f'cat /proc/{pid}/status')
        sample[name + '_rss_kib'] = int(re.search(r'^VmRSS:\s+(\d+) kB', status, re.M)[1])
        baseline = memory[0][name + '_fds'] if memory else None
        fd_profile = descriptors(observe,pid,baseline)
        for field, value in fd_profile.items():
            sample[name + '_' + field] = value
    memory.append(sample)
    (folder / 'hardware-memory.json').write_text(json.dumps(memory, indent=2) + '\n')


try:
    if observe('cat /tmp/CORENAME').strip() != 'TIC-80':
        raise RuntimeError('TIC-80 must already be selected')
    parent = supervisor()
    assert parent.isdigit()
    hashes = {}
    for name, local, remote in [('player', 'build/arm/tic80-live', '/media/fat/games/TIC-80/TIC-80'),
                          ('rbf', 'build/fpga/output_files/TIC80.rbf', '/media/fat/_Other/TIC80_20260930.rbf'),
                          ('monitor', 'build/arm/tic80-runtime-monitor', '/tmp/tic80-mister-dev/runtime-monitor')]:
        hashes[name] = hashlib.sha256((ROOT / local).read_bytes()).hexdigest()
        assert observe('sha256sum ' + remote).split()[0] == hashes[name]
    saveid = (folder / 'saveid.txt').read_text().strip()
    assert saveid.startswith('tic80-soak-') and all(ch.isalnum() or ch == '-' for ch in saveid)
    savepath = '/media/fat/saves/TIC-80/' + hashlib.md5(saveid.encode()).hexdigest() + '.pmem'
    cartridge = folder / 'music-soak.tic'
    marker = f'Cartridge loaded: {cartridge.stat().st_size} bytes; reset=0'
    with c.open_sftp() as sftp:
        # UUID saveid prevents prior runs from biasing this measurement.
        try:
            sftp.stat(savepath)
        except FileNotFoundError:
            pass
        else:
            raise RuntimeError('Generate a fresh soak cartridge for each run')
        remote = '/tmp/tic80-mister-dev/music-soak'
        sftp.put(str(cartridge), remote + '.tic')
        with sftp.open(remote + '.mgl', 'w') as file:
            file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                       f' <file delay="3" type="f" index="0" path="{remote}.tic"/>\n'
                       '</mistergamedescription>\n')
        before = observe('cat /media/fat/logs/TIC-80/tic80.log')
        guard()
        command('test "$(cat /tmp/CORENAME)" = TIC-80 || exit 30; '
                f'printf "load_core {remote}.mgl\\n" > /dev/MiSTer_cmd')
        deadline = time.monotonic() + 35
        while True:
            guard()
            log = observe('cat /media/fat/logs/TIC-80/tic80.log')
            if log.count(marker) > before.count(marker):
                break
            if time.monotonic() > deadline:
                raise RuntimeError(log)
            time.sleep(.25)
        time.sleep(2)  # Cold cartridge switch excluded from the steady interval.
        guard()
        saves.append(saved())
        (folder / 'hardware-saves.json').write_text(json.dumps(saves, indent=2) + '\n')
        observer = MonitorJournal(command, a.seconds, a.interval_ms, read_command=observe) if a.remote_journal else None
        if observer:
            print('Bounded monitor journal: ' + observer.directory, flush=True)
            samples = observer.rows()
        else:
            _, out, err = c.exec_command(f'/tmp/tic80-mister-dev/runtime-monitor --seconds {a.seconds} --interval-ms {a.interval_ms}', timeout=45)
            samples = (json.loads(line) for line in out)
        next_check = 0
        with (folder / 'hardware-samples.jsonl').open('w') as journal:
            for row in samples:
                rows.append(row)
                journal.write(json.dumps(row) + '\n')
                journal.flush()
                elapsed = row['elapsed_ns']/1e9
                if elapsed >= next_check:
                    guard()
                    current = saved()
                    assert current['ticks'] >= saves[-1]['ticks'] and current['elapsed_ms'] >= saves[-1]['elapsed_ms']
                    saves.append(current)
                    (folder / 'hardware-saves.json').write_text(json.dumps(saves, indent=2) + '\n')
                    process_memory(elapsed)
                    gaps = (row['underruns'] - rows[0]['underruns']) & 0xffffffff
                    print(f'{elapsed:.1f}s: {current["ticks"]} game ticks; {gaps} new underrun slots; save CRC valid', flush=True)
                    next_check += 30
        if not observer:
            errors = err.read().decode()
            assert out.channel.recv_exit_status() == 0, errors
        guard()
        assert len(rows) >= a.seconds*4
        log = observe('cat /media/fat/logs/TIC-80/tic80.log')
        after = log[len(before):] if log.startswith(before) else log
        assert all(text not in after for text in ('Cartridge failed', 'Cartridge rejected', 'Save error', 'TIC-80:')), after
        # Flush on leaving the core, then inspect the last atomic save.
        command('test "$(cat /tmp/CORENAME)" = TIC-80 || exit 30; printf "load_core /media/fat/menu.rbf\\n" > /dev/MiSTer_cmd')
        deadline = time.monotonic() + 15
        while observe('cat /tmp/CORENAME').strip() != 'MENU' or supervisor():
            if observe('cat /tmp/CORENAME').strip() not in ('MENU', 'TIC-80'):
                raise RuntimeError('Another core selected during final flush')
            if time.monotonic() > deadline:
                raise RuntimeError('Supervisor did not stop in MENU')
            time.sleep(.2)
        final = saved()
        saves.append(final)
        (folder / 'hardware-saves.json').write_text(json.dumps(saves, indent=2) + '\n')
    first, last = rows[0], rows[-1]
    seconds = (last['elapsed_ns']-first['elapsed_ns'])/1e9
    delta = lambda name: (last[name]-first[name]) & 0xffffffff
    result = dict(hashes=hashes, seconds=seconds, samples=len(rows), session=first['session'],
                  audio_clock_hz=delta('slots')/seconds, underrun_slots=delta('underruns'),
                  startup_underrun_slots=first['underruns'], played_sample_frames=delta('played'),
                  game_frames=((last['presented']>>2)-(first['presented']>>2)) & 0x3fffffff,
                  carrier_frames=delta('heartbeat'), final_save=final, saves=saves,
                  process_memory=memory, sample_interval_ms=a.interval_ms,
                  audio_queue=analyze((folder / 'hardware-samples.jsonl').read_bytes()),
                  observer_reconnects=observer_reconnects,
                  remote_journal=observer.directory if observer else None)
    (folder / 'hardware-result.json').write_text(json.dumps(result, indent=2) + '\n')
    assert len({r['session'] for r in rows}) == 1
    assert all(abs(((r['slots']-r['underruns']) & 0xffffffff)-r['played']) <= 2 for r in rows)
    assert result['startup_underrun_slots'] == 0, result
    assert result['underrun_slots'] == 0, result
    if a.synchronized_video:
        result['video_audio_alignment'] = analyze_video(rows)
        (folder / 'hardware-result.json').write_text(json.dumps(result, indent=2) + '\n')
    if a.seconds >= 600:
        assert a.interval_ms == 197, 'Clock qualification requires sampling across tick phases'
        assert abs(result['audio_queue']['fitted_queue_change_ms']) <= 2, result
    assert abs(result['audio_clock_hz']-48000) < 24, result  # 500 ppm, including snapshot timestamp error.
    assert abs(result['game_frames']/seconds-60) < .1, result
    assert final['largest_gap_ms'] < 250, result
    assert len({r['worker'] for r in memory}) == 1, memory
    steady = [r for r in memory if r['seconds'] >= 60]
    if len(steady) >= 2:
        for name, limit in [('parent', 256), ('worker', 2048)]:
            rss = [r[name + '_rss_kib'] for r in steady]
            assert max(rss) - min(rss) <= limit, memory
            assert max(r[name + '_fds'] for r in steady) <= memory[0][name + '_fds'] + 1, memory
    print('Music, clock, video publication and persistent-save soak passed: ' + json.dumps({k:v for k,v in result.items() if k not in ('saves', 'process_memory')}), flush=True)
finally:
    c.close()
