"""Exercise installed Studio cold entry, cartridge switches and FPGA reloads.

Use a unique saveid and private fixture directory; preserve installed payloads,
the handler, settings and user saves. Never retry a remote mutation. The final
action restores the installed Tetris MGL when MENU/TIC-80 still owns the board.
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import struct
import sys
import time
import uuid
import zlib

import paramiko

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from service_png_test import modern, legacy
from service_recovery_test import ordinary
from hardware_ssh import command, connect
from hardware_source import source_snapshot
from hardware_process import descriptors
from install_studio_candidate import Installer


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--cycles', type=int, default=4)
    parser.add_argument('--reloads', type=int, default=16)
    args = parser.parse_args()
    assert 1 <= args.cycles <= 32 and 8 <= args.reloads <= 64
    b = ROOT / 'build'
    installed_path = b / 'studio-installed-latest.json'
    installed = json.loads(installed_path.read_text())
    plan_path = b / installed['plan']; plan = json.loads(plan_path.read_text())
    assert plan['host'] == args.host and plan['installation_complete']
    assert installed['frontend'] == 'studio'
    profile = json.loads((b / 'mgl-popup-native-latest.json').read_text())
    prefix = 'studio-installed-lifecycle-' + uuid.uuid4().hex[:8]
    folder = b / prefix; folder.mkdir()
    remote = '/media/fat/games/TIC-80/.native-tests/' + prefix
    key = prefix + '-shared'
    savepath = '/media/fat/saves/TIC-80/' + hashlib.md5(key.encode()).hexdigest() + '.pmem'
    native = ordinary(key, 8)
    payloads = [('native', native, 'tic', 0), ('modern', modern(native), 'png', 64),
                ('legacy', legacy(native), 'png', 64)]
    manifest = {}
    for label, data, ext, index in payloads:
        (folder / (label + '.' + ext)).write_bytes(data)
        path = remote + '/' + label + '.' + ext
        mgl = ('<mistergamedescription>\n <rbf>_Other/TIC80_20261003</rbf>\n'
               f' <file delay="3" type="f" index="{index}" path="{path}"/>\n'
               '</mistergamedescription>\n')
        (folder / (label + '.mgl')).write_bytes(mgl.encode())
        manifest[label] = dict(path=path, bytes=len(data), sha256=digest(data),
                               mgl_sha256=digest(mgl.encode()))
    (folder / 'fixtures.json').write_text(json.dumps(dict(saveid=key, savepath=savepath,
        fixtures=manifest), indent=2) + '\n')
    (folder / 'driver.py').write_bytes(Path(__file__).read_bytes())
    (folder / 'installed-before.json').write_bytes(installed_path.read_bytes())
    (folder / 'plan-before.json').write_bytes(plan_path.read_bytes())
    result = dict(started_at=datetime.now(timezone.utc).isoformat(), passed=False,
        prefix=prefix, folder=prefix, cycles=args.cycles, reloads_per_cycle=args.reloads,
        installed_state_sha256=digest(installed_path.read_bytes()), payload=installed['payload'],
        saveid=key, savepath=savepath, actions=[], sessions=[], transitions=[],
        mutations_retried=False, canonical_handler_replaced=False)
    c = paramiko.SSHClient(); c.load_system_host_keys()
    if (b / 'ssh_known_hosts').exists(): c.load_host_keys(str(b / 'ssh_known_hosts'))
    connect(c, args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
    i = Installer(c, Path(plan['package']), plan_path, plan['stage'].split('/')[-1])
    run = i.run
    monitor = profile['remote'] + '/runtime-monitor'
    logpath = '/media/fat/logs/TIC-80/tic80.log'
    restore = plan['stage'] + '/installed-tetris.mgl'
    restore_local = b / 'studio-checkpoint-strong-canonical-final-20261004/tetris.mgl'
    touched = False

    def save_progress():
        (folder / 'progress.json').write_text(json.dumps(result, indent=2) + '\n')

    def guard():
        assert i.selected() in ('MENU', 'TIC-80')
        assert i.snapshot(plan['after']) == plan['after'], 'Installed payload/settings changed'
        i.running_main()

    def load(path, label):
        nonlocal touched
        selected = i.selected()
        result['current_action'] = dict(label=label, path=path, command_started=True,
            started_at=datetime.now(timezone.utc).isoformat())
        save_progress(); touched = True
        run('test "$(cat /tmp/CORENAME)" = ' + selected + ' && printf ' +
            shlex.quote('load_core ' + path + '\n') + ' > /dev/MiSTer_cmd')

    def parents():
        return run('for p in $(pidof TIC-80-Studio); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80-Studio && printf "%s " "$p"; done; true').split()

    def log():
        return run('cat ' + logpath + ' 2>/dev/null || true')

    def ready(expected_parent=None, console=False):
        deadline = time.monotonic() + 40
        while True:
            i.selected(); pids = parents(); text = log()
            needle = 'TIC-80 Studio live ready: mode=1' if console else 'MiSTer cartridge running:'
            if len(pids) == 1 and needle in text:
                pid = pids[0]
                if expected_parent is not None: assert pid == expected_parent, 'Same-core reload replaced supervisor'
                assert run('sha256sum /proc/' + pid + '/exe').split()[0] == installed['payload']['games/TIC-80/TIC-80-Studio']['sha256']
                return pid
            assert time.monotonic() < deadline, ('Frontend not ready', text)
            time.sleep(.2)

    def read_save():
        with c.open_sftp() as s:
            with s.open(savepath, 'rb') as f: data = f.read()
        assert len(data) == 1036 and data[:4] == b'TMPM'
        assert struct.unpack_from('<I', data, 8)[0] == zlib.crc32(data[12:])
        values = struct.unpack_from('<256I', data, 12)
        return data, values

    def settled_save(boots, min_ticks, pid):
        deadline = time.monotonic() + 30
        while True:
            assert i.selected() == 'TIC-80' and parents() == [pid]
            try: data, values = read_save()
            except FileNotFoundError: values = None
            if values:
                assert values[1] <= boots, ('Duplicate BOOT', values[:2], boots)
                if values[1] == boots and values[0] >= min_ticks: return data, values
            assert time.monotonic() < deadline, ('Save/tick progress missing', values, boots, log())
            time.sleep(.2)

    def placement_memory(pid, baseline=None):
        workers = run("ps -eo pid,ppid,comm | awk '$2 == " + pid + " && $3 == \"tic80-studio\" {print $1}'").split()
        assert len(workers) == 1 and workers[0].isdigit(), workers
        answer = dict(parent=pid, worker=workers[0])
        for name, process, expected_nice in [('parent', pid, -20), ('worker', workers[0], -10)]:
            status = run('cat /proc/' + process + '/status')
            cpu = re.search(r'^Cpus_allowed_list:\s+(\S+)', status, re.M)[1]
            stat = run('cat /proc/' + process + '/stat')
            fields = stat[stat.rfind(')') + 2:].split()
            nice = int(fields[16]); assert cpu == '1' and nice == expected_nice
            assert 'SCHED_OTHER' in run('chrt -p ' + process)
            answer[name + '_cpu'] = cpu; answer[name + '_nice'] = nice
            answer[name + '_starttime'] = int(fields[19])
            answer[name + '_rss_kib'] = int(re.search(r'^VmRSS:\s+(\d+) kB', status, re.M)[1])
            old = baseline[name + '_fds'] if baseline else None
            for k, v in descriptors(run, process, old).items(): answer[name + '_' + k] = v
        return answer

    def observe(label, pid, baseline=None):
        assert i.selected() == 'TIC-80' and parents() == [pid]
        raw = run(monitor + ' --seconds 2 --interval-ms 13')
        (folder / (label + '-audio.jsonl')).write_text(raw)
        rows = [json.loads(x) for x in raw.splitlines()]
        assert len(rows) >= 100 and len({r['session'] for r in rows}) == 1
        assert all(r['underruns'] == 0 for r in rows)
        assert ((rows[-1]['played'] - rows[0]['played']) & 0xffffffff) > 90000
        assert ((rows[-1]['heartbeat'] - rows[0]['heartbeat']) & 0xffffffff) > 100
        memory = placement_memory(pid, baseline)
        text = log(); (folder / (label + '.log')).write_text(text)
        assert all(v not in text for v in ('initialization did not complete', 'rejected;',
            'error=1', 'Studio log: dropped=', 'waiting for unsaved-changes confirmation')), text
        return dict(samples=len(rows), first=rows[0], last=rows[-1], **memory)

    def transition(label):
        trace = remote + '/' + label + '-transition'
        script = (remote + '/observer 10 ' + trace + '.jsonl; status=$?; '
                  'printf "%s\\n" "$status" > ' + trace + '.status.tmp; '
                  'mv ' + trace + '.status.tmp ' + trace + '.status')
        assert i.selected() == 'TIC-80'
        pid = run('test "$(cat /tmp/CORENAME)" = TIC-80 && { nohup taskset 2 nice -n 19 sh -c ' +
            shlex.quote(script) + ' > ' + trace + '.stderr 2>&1 < /dev/null & printf "%s\\n" "$!"; }').strip()
        assert pid.isdigit()
        journal = dict(label=label, remote=trace, pid=pid, started_once=True, collected=False)
        result['transitions'].append(journal); save_progress()
        deadline = time.monotonic() + 5
        while not run('test -s ' + trace + '.jsonl && echo ready || true').strip():
            assert time.monotonic() < deadline, 'Original transition observer did not start'
            time.sleep(.1)

    def collect_transitions():
        for j in result['transitions']:
            if j['collected']: continue
            trace = j['remote']; deadline = time.monotonic() + 35
            while not run('test -f ' + trace + '.status && echo complete || true').strip():
                assert time.monotonic() < deadline, 'Original transition observer remains pending: ' + j['pid']
                time.sleep(.25)
            with c.open_sftp() as s:
                for suffix in ('.jsonl', '.status', '.stderr'):
                    s.get(trace + suffix, str(folder / (j['label'] + '-transition' + suffix)))
            assert (folder / (j['label'] + '-transition.status')).read_text().strip() == '0'
            rows = [json.loads(x) for x in (folder / (j['label'] + '-transition.jsonl')).read_text().splitlines()]
            coherent = [r for r in rows if r['coherent_audio'] and r['selected']]
            assert len(coherent) >= 100 and all(r['underruns'] == 0 for r in coherent)
            j.update(collected=True, samples=len(rows), coherent_samples=len(coherent),
                     sessions=sorted({r['session'] for r in coherent}))
            save_progress()

    try:
        guard()
        assert run('sha256sum ' + monitor).split()[0] == profile['candidates']['runtime-monitor']
        # The prior Windows artifact uses CRLF; the staged XML was sent as LF.
        assert run('sha256sum ' + restore).split()[0] == digest(restore_local.read_text().encode())
        assert run('test ! -e ' + remote + ' && test ! -e ' + savepath + ' && echo unique').strip() == 'unique'
        run('test "$(cat /tmp/CORENAME)" = ' + i.selected() + ' && mkdir -p ' + remote)
        with c.open_sftp() as s:
            for label, data, ext, index in payloads:
                s.put(str(folder / (label + '.' + ext)), remote + '/' + label + '.' + ext)
                s.put(str(folder / (label + '.mgl')), remote + '/' + label + '.mgl')
                assert run('sha256sum ' + remote + '/' + label + '.' + ext).split()[0] == digest(data)
                assert run('sha256sum ' + remote + '/' + label + '.mgl').split()[0] == manifest[label]['mgl_sha256']
            s.put(str(b / 'audio-reload-observer'), remote + '/observer')
        assert run('sha256sum ' + remote + '/observer').split()[0] == digest((b / 'audio-reload-observer').read_bytes())
        run('chmod +x ' + remote + '/observer')
        previous_ticks = 0; expected_boots = 0; prior_parent = None
        for cycle in range(args.cycles):
            guard(); i.menu()
            load(installed['core_rbf'], f'cycle-{cycle}-cold-console')
            pid = ready(console=True); assert pid != prior_parent
            console = observe(f'cycle-{cycle}-console', pid)
            session = dict(cycle=cycle, parent=pid, console=console, memory=[])
            result['sessions'].append(session); save_progress()
            print(f'Cycle {cycle + 1}/{args.cycles}: cold console ready, parent {pid}', flush=True)
            for label, data, ext, index in payloads:
                name = f'cycle-{cycle}-{label}'
                if cycle == 0: transition(name)
                load(remote + '/' + label + '.mgl', name)
                ready(pid); expected_boots += 1
                save_data, values = settled_save(expected_boots, previous_ticks + 60, pid)
                state = observe(name, pid)
                source = source_snapshot(run, manifest[label]['path'], len(data))
                action = dict(label=name, cycle=cycle, kind=label, boots=values[1],
                    ticks=values[0], source=source, observation=state)
                result['actions'].append(action); session['memory'].append(action)
                previous_ticks = values[0]; save_progress()
                print(f'{name}: BOOT {expected_boots}, {previous_ticks} ticks, zero underruns', flush=True)
            baseline = None
            for ordinal in range(args.reloads):
                name = f'cycle-{cycle}-reload-{ordinal}'
                if ordinal in (0, args.reloads - 1): transition(name)
                load(installed['core_rbf'], name); expected_boots += 1
                save_data, values = settled_save(expected_boots, previous_ticks + 60, pid)
                state = observe(name, pid, baseline)
                if ordinal == 4: baseline = state
                action = dict(label=name, cycle=cycle, kind='raw-reload', ordinal=ordinal,
                              boots=values[1], ticks=values[0], observation=state)
                result['actions'].append(action); session['memory'].append(action)
                previous_ticks = values[0]; save_progress()
                print(f'{name}: BOOT {expected_boots}, {previous_ticks} ticks, zero underruns', flush=True)
            collect_transitions()
            i.menu(); text = log(); (folder / f'cycle-{cycle}-exit.log').write_text(text)
            assert 'departed=1 error=0' in text and 'Studio RUN: waiting_frames=0' in text
            assert 'Studio log: dropped=' not in text
            assert re.search(r'Studio live stopped:.*recoveries=0\b', text)
            flushed, values = read_save()
            assert values[1] == expected_boots and values[0] >= previous_ticks
            previous_ticks = values[0]
            (folder / f'cycle-{cycle}-final.pmem').write_bytes(flushed)
            steady = [a['observation'] for a in session['memory'] if a.get('ordinal', -1) >= 4]
            for name, limit in [('parent', 128), ('worker', 256)]:
                rss = [s[name + '_rss_kib'] for s in steady]
                assert max(rss) - min(rss) <= limit, (name, 'RSS did not plateau', rss)
            session.update(passed=True, final_boots=values[1], final_ticks=values[0],
                run_waiting_frames=0, recoveries=0, clean_departure=True)
            prior_parent = pid; guard(); save_progress()
        assert len(result['actions']) == args.cycles * (3 + args.reloads)
        result.update(passed=True, final_boots=expected_boots, final_ticks=previous_ticks,
            completed_at=datetime.now(timezone.utc).isoformat(), installed_payloads_preserved=True,
            canonical_handler_preserved=True)
        print(f'Installed Studio lifecycle passed: {args.cycles} cold entries, '
              f'{args.cycles * 3} cartridge switches, {args.cycles * args.reloads} raw reloads', flush=True)
    except Exception as error:
        result['error'] = repr(error); save_progress(); raise
    finally:
        try:
            if touched:
                collect_transitions(); guard(); i.menu()
                load(restore, 'restore-installed-tetris')
                pid = ready()
                result['restored_tetris'] = observe('restored-tetris', pid)
                result['restored_source'] = source_snapshot(run, '/media/fat/games/TIC-80/Carts/tetris.tic', 25147)
                guard(); result['tetris_restored_verified'] = True
        finally:
            (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            (b / 'studio-installed-lifecycle-latest.json').write_text(json.dumps(result, indent=2) + '\n')
            c.close()


if __name__ == '__main__': main()
