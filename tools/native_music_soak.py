"""Read-only measurements for a privately staged, hash-bound native frontend."""
import hashlib
import json
import re
import struct
import time
import zlib
from analyze_audio_queue import analyze
from analyze_video_clock import analyze as alignment
from hardware_process import descriptors


def read_save(test, path):
    def read():
        with test.client.open_sftp() as sftp:
            with sftp.open(path, 'rb') as stream:
                return stream.read(1037)
    data = test.read_operation(read)
    assert len(data) == 1036 and data[:4] == b'TMPM'
    assert struct.unpack_from('<I', data, 4)[0] == 1
    assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
    ticks, elapsed, boots, gap = struct.unpack_from('<4I', data, 12)
    assert boots == 1
    return dict(ticks=ticks, elapsed_ms=elapsed, boots=boots, largest_gap_ms=gap)


def validate_samples(payload, seconds):
    rows = [json.loads(line) for line in payload.splitlines()]
    assert len(rows) >= seconds * 4
    assert len({row['session'] for row in rows}) == 1
    first, last = rows[0], rows[-1]
    duration = (last['elapsed_ns'] - first['elapsed_ns']) / 1e9
    assert duration >= seconds - 1
    delta = lambda name: (last[name] - first[name]) & 0xffffffff
    assert all(row['underruns'] == 0 for row in rows)
    assert all(abs(((r['slots'] - r['underruns']) & 0xffffffff) - r['played']) <= 2 for r in rows)
    clock = delta('slots') / duration
    frames = ((last['presented'] >> 2) - (first['presented'] >> 2)) & 0x3fffffff
    assert abs(clock - 48000) < 24
    assert abs(frames / duration - 60) < .1
    queue = analyze(payload.encode())
    if seconds >= 600:
        assert abs(queue['fitted_queue_change_ms']) <= 2
    return dict(seconds=duration, samples=len(rows), audio_clock_hz=clock,
                underrun_slots=0, game_fps=frames / duration, audio_queue=queue,
                video_audio_alignment=alignment(rows))


def run_soak(test, args, stage, prefix, save_path, identity, main_pid, mgl, frontend):
    deadline = time.monotonic() + 25
    while True:
        try:
            first_save = read_save(test, save_path)
            if first_save['ticks'] >= 120:
                break
        except FileNotFoundError:
            pass
        assert test.core() == 'TIC-80' and test.main_pid() == main_pid
        assert time.monotonic() < deadline, 'Music did not begin; original frontend was not restarted'
        time.sleep(.25)
    # The monitor never writes DDR or acquires output ownership. Its journal
    # and the frontend are original, separately bounded jobs.
    test.job(prefix, 'taskset 1 nice -n 19 ' + stage + '/monitor --seconds ' + str(args.soak_seconds) +
             ' --interval-ms 197 > ' + prefix + '.jsonl 2> ' + prefix + '.stderr')
    started = time.monotonic()
    deadline = started + args.soak_seconds + 45
    memories, saves = [], [first_save]
    worker_name = 'tic80-vm' if frontend == 'player' else 'tic80-studio'
    next_check = 0
    while True:
        status = test.run('if test -f ' + prefix + '.status; then cat ' + prefix + '.status; fi').strip()
        if status:
            break
        elapsed = time.monotonic() - started
        assert time.monotonic() < deadline, 'Original bounded monitor did not finish; do not restart'
        if elapsed >= next_check:
            assert test.core() == 'TIC-80' and test.main_pid() == main_pid
            assert mgl in test.argv(main_pid), 'Another MGL selected'
            assert test.frontend_identity(identity['pid'], identity['sha256']) == identity
            current = read_save(test, save_path)
            assert current['ticks'] >= saves[-1]['ticks'] and current['elapsed_ms'] >= saves[-1]['elapsed_ms']
            saves.append(current)
            listing = test.run('ps -eo pid,ppid,comm').splitlines()
            workers = [row.split()[0] for row in listing if len(row.split()) == 3 and
                       row.split()[1] == identity['pid'] and row.split()[2] == worker_name]
            assert len(workers) == 1
            memory = dict(seconds=elapsed, worker=workers[0], worker_birth=test.birth(workers[0]))
            for name, pid in [('parent', identity['pid']), ('worker', workers[0])]:
                memory[name + '_rss_kib'] = int(re.search(r'^VmRSS:\s+(\d+) kB', test.run('cat /proc/' + pid + '/status'), re.M)[1])
                baseline = memories[0][name + '_fds'] if memories else None
                memory.update({name + '_' + field: value for field, value in descriptors(test.run, pid, baseline).items()})
            memories.append(memory)
            (args.evidence / (prefix.rsplit('/', 1)[1] + '-resources.json')).write_text(json.dumps(dict(memory=memories, saves=saves), indent=2) + '\n')
            print(f'{frontend}: {elapsed:.0f}s music; {current["ticks"]} ticks; original interpreter {workers[0]}', flush=True)
            next_check = elapsed + 30
        time.sleep(1)
    assert test.collect(prefix, 5) == 0, test.run('cat ' + prefix + '.stderr')
    payload = test.run('cat ' + prefix + '.jsonl')
    (args.evidence / (prefix.rsplit('/', 1)[1] + '.jsonl')).write_text(payload)
    (args.evidence / (prefix.rsplit('/', 1)[1] + '.stderr')).write_text(test.run('cat ' + prefix + '.stderr'))
    result = validate_samples(payload, args.soak_seconds)
    final_save = read_save(test, save_path)
    assert final_save['ticks'] > first_save['ticks'] + args.soak_seconds * 59
    assert final_save['largest_gap_ms'] < 250
    assert len({(row['worker'], row['worker_birth']) for row in memories}) == 1
    steady = [row for row in memories if row['seconds'] >= 60]
    if len(steady) >= 2:
        for name, limit in [('parent', 256), ('worker', 2048)]:
            values = [row[name + '_rss_kib'] for row in steady]
            assert max(values) - min(values) <= limit
    return dict(result, final_save=final_save, worker=memories[0]['worker'],
                resources_sha256=hashlib.sha256(json.dumps(memories, sort_keys=True).encode()).hexdigest())
