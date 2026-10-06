"""Test real player/Studio mouse APIs and FPGA OSD with stock Main.

All candidate payloads, cartridges, logs and saves are private. The installed
release and shared Main are never replaced. Frontier is paused at MENU while
the explicit test frontend owns the core, then resumed before restoration.
Each original job is dispatched once and collected through its status journal.
"""
import argparse
import errno
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import struct
import time
import uuid
import paramiko
from hardware_ssh import command, connect
from test_hid_pan_native import STOCK, cartridge_metadata
from analyze_hdmi_clock import analyze

ROOT = Path(__file__).resolve().parents[1]
STAGES = [('coarse-positive', -1), ('coarse-negative', 0), ('fine-fractions', 0),
          ('fine-complete', -2), ('stalled-device-isolation', -3),
          ('menu-motion-suppressed', -3), ('menu-backlog-suppressed', -3),
          ('fractions-cleared-after-menu', -3), ('post-menu-fine', -5)]


def validate_rows(text, main_pid, frontend_pid):
    rows = [json.loads(line) for line in text.splitlines()]
    owners = [row for row in rows if 'main_fd_verified' in row]
    assert len(owners) == 5
    assert all(row['main_pid'] == int(main_pid) and row['main_fd_verified'] is True and
               row['evdev_grab_busy'] is True and row['only_main_and_probe_evdev_fds'] is True for row in owners)
    assert len({row['event'] for row in owners}) == len({row['raw'] for row in owners}) == 5
    stages = [row for row in rows if 'stage' in row]
    assert [(row['stage'], row['pan']) for row in stages] == STAGES
    assert all(row['boot'] == 1 and row['repeat_mismatches'] == 0 for row in stages)
    assert all(right['ticks'] >= left['ticks'] + 60 for left, right in zip(stages, stages[1:]))
    gates = [row for row in rows if 'osd_open' in row]
    assert len(gates) == 2 and gates[0]['osd_open'] is True and gates[1]['osd_open'] is False
    assert (gates[1]['epoch'] - gates[0]['epoch']) & 4095 == 1
    final = rows[-1]
    assert final['native_frontend_passed'] is True and final['pan'] == -5
    assert final['main_pid'] == int(main_pid) and final['frontend_pid'] == int(frontend_pid)
    assert final['blocked_feature_gets'] > 0
    assert len(rows) == 17
    return rows


def cartridge(saveid):
    code = f'''-- script: lua
-- saveid: {saveid}
function BOOT() pmem(0,0x48696450); pmem(1,pmem(1)+1) end
function TIC()
 local x,y,l,m,r,sx,sy=mouse()
 local xx,yy,ll,mm,rr,ssx,ssy=mouse()
 if x~=xx or y~=yy or l~=ll or m~=mm or r~=rr or sx~=ssx or sy~=ssy then pmem(10,pmem(10)+1) end
 pmem(2,pmem(2)+1); pmem(3,pmem(3)+sx); pmem(4,pmem(4)+sy)
 pmem(5,x); pmem(6,y); pmem(7,(l and 1 or 0)+(m and 2 or 0)+(r and 4 or 0))
 cls(0); print('TIC-80 actual mouse API',10,12,12)
 print('pan total '..pmem(3),10,30,11); print('ticks '..pmem(2),10,44,11)
end
'''.encode()
    return bytes([5]) + struct.pack('<H', len(code)) + b'\0' + code


class PrivateTest:
    def __init__(self, client, evidence):
        self.client, self.evidence = client, evidence
        self.result = {'passed': False, 'dispatches': [], 'observations': [],
                       'installed_payloads_replaced': False, 'shared_Main_replaced': False}

    def save(self):
        (self.evidence / 'result.json').write_text(json.dumps(self.result, indent=2) + '\n')

    def run(self, text, timeout=45):
        return command(self.client, text, timeout=timeout)

    def read(self, text):
        value = self.run(text)
        self.result['observations'].append({'command': text, 'output': value})
        self.save()
        return value

    def dispatch(self, name, text):
        self.result['dispatches'].append({'name': name, 'command': text, 'at': time.time()})
        self.save()
        return self.run(text)  # Never retry a launch, signal or load after a lost reply.

    def core(self):
        return self.run('cat /tmp/CORENAME').strip()

    def main_pid(self):
        deadline = time.monotonic() + 30
        while True:
            assert self.run('sha256sum /media/fat/MiSTer').split()[0] == STOCK
            pids = self.run('pidof MiSTer || true').split()
            verified = []
            for pid in pids:
                assert pid.isdigit()
                words = self.run('if test -e /proc/' + pid + '/exe; then sha256sum /proc/' + pid + '/exe; fi').split()
                assert not words or words[0] == STOCK
                if words:
                    verified.append(pid)
            if len(pids) == 1 and verified == pids and self.run('pidof MiSTer').split() == pids:
                return pids[0]
            assert time.monotonic() < deadline, ('Main did not settle', pids)
            time.sleep(.2)

    def load(self, path, selected):
        assert self.core() == selected
        self.main_pid()
        self.dispatch('load:' + path, 'test "$(cat /tmp/CORENAME)" = ' + shlex.quote(selected) +
                      ' && printf %s ' + shlex.quote('load_core ' + path + '\n') + ' > /dev/MiSTer_cmd')

    def wait_core(self, expected, allowed):
        deadline = time.monotonic() + 40
        while True:
            selected = self.core()
            assert selected in allowed, ('Another core selected', selected)
            if selected == expected:
                return
            assert time.monotonic() < deadline, ('Core did not settle', expected, selected)
            time.sleep(.2)

    def snapshot(self, names):
        return {path: self.run('sha256sum -- ' + shlex.quote('/media/fat/' + path)).split()[0] for path in names}

    def argv(self, pid):
        try:
            with self.client.open_sftp() as sftp:
                with sftp.open('/proc/' + str(pid) + '/cmdline', 'rb') as stream:
                    return [word.decode() for word in stream.read().split(b'\0') if word]
        except OSError as error:
            if error.errno in (errno.ENOENT, errno.ESRCH):
                return []  # A read-only process inventory can race ordinary exit.
            raise

    def frontend_identity(self, pid, expected):
        assert pid.isdigit() and int(pid) > 1
        birth = self.birth(pid)
        assert self.run('sha256sum /proc/' + pid + '/exe').split()[0] == expected
        assert self.birth(pid) == birth
        return {'pid': pid, 'birth': birth, 'sha256': expected}

    def stop_frontend(self, identity, name):
        pid = identity['pid']
        assert self.frontend_identity(pid, identity['sha256']) == identity
        # Recheck start time inside the same command before sending the signal.
        guard = 'test "$(sed "s/.*) //" /proc/' + pid + '/stat | awk \'{print $20}\')" = ' + identity['birth']
        self.dispatch(name, guard + ' && kill -TERM ' + pid)

    def frontier_idle(self, pid):
        assert pid.isdigit()
        # MiSTer's kernel omits CONFIG_CHECKPOINT_RESTORE, so the optional
        # /proc/PID/task/PID/children file is unavailable. PPID in stat is
        # always present. A vanished entry during inventory is harmless.
        children = self.run('for p in /proc/[0-9]*/stat; do sed -n '
                            + shlex.quote(r's/^\([0-9]*\) (.*) [^ ]* ' + pid + r' .*/\1/p')
                            + ' "$p" 2>/dev/null; done').split()
        return all(idle_child(self.argv(child)) for child in children)

    def birth(self, pid):
        text = self.run('cat /proc/' + str(pid) + '/stat')
        return text[text.rfind(')') + 2:].split()[19]

    def cache(self, path):
        try:
            with self.client.open_sftp() as sftp:
                with sftp.open(path, 'rb') as stream:
                    data = stream.read(65537)
                    assert len(data) <= 65536, 'Unexpected cache size'
                    return data
        except OSError as error:
            if error.errno == errno.ENOENT:
                return None
            raise

    def job(self, prefix, launch):
        script = (f'printf "%s\\n" "$$" > {prefix}.job.pid\n' + launch + '\n' +
                  f'status=$?\nprintf "%s\\n" "$status" > {prefix}.status.tmp\nmv {prefix}.status.tmp {prefix}.status\n')
        with self.client.open_sftp() as sftp:
            with sftp.open(prefix + '.sh', 'wb') as stream:
                stream.write(script.encode())
        self.dispatch('original-job:' + prefix, 'nohup sh ' + shlex.quote(prefix + '.sh') +
                      ' > ' + shlex.quote(prefix + '.coordinator.log') + ' 2>&1 < /dev/null &')

    def collect(self, prefix, seconds):
        deadline = time.monotonic() + seconds
        while True:
            text = self.run('if test -f ' + prefix + '.status; then cat ' + prefix + '.status; fi').strip()
            if text:
                status = int(text)
                self.result.setdefault('original_jobs', {})[prefix] = {'exit': status}
                self.save()
                return status
            pid = self.run('if test -f ' + prefix + '.job.pid; then cat ' + prefix + '.job.pid; fi').strip()
            if not pid:
                assert time.monotonic() < deadline, 'Original launch has no PID or terminal journal; do not retry'
                time.sleep(.2)
                continue
            assert pid.isdigit()
            live = self.run('kill -0 ' + pid + ' 2>/dev/null && echo live || true').strip()
            self.result.setdefault('original_jobs', {})[prefix] = {'pid': pid, 'confirmed_live': live == 'live'}
            self.save()
            if live != 'live':
                # The job can publish its journal and exit between our first
                # status read and liveness check. Observe that same journal
                # once more before classifying a missing process as failure.
                terminal = self.run('if test -f ' + prefix + '.status; then cat ' + prefix + '.status; fi').strip()
                assert terminal, 'Original job vanished without terminal journal'
                status = int(terminal)
                self.result['original_jobs'][prefix] = {'pid': pid, 'exit': status, 'exit_race_reconciled': True}
                self.save()
                return status
            assert time.monotonic() < deadline, 'Observation budget expired; original job was not restarted'
            time.sleep(.3)


def idle_child(argv):
    # Frontier's normal one-second sleep is harmless at MENU. A handler or
    # unknown command is still a live owner and must finish before pausing.
    return not argv or (len(argv) == 2 and PurePosixPath(argv[0]).name == 'sleep' and argv[1] == '1')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--probe-build', type=Path, required=True)
    parser.add_argument('--reader-evidence', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--initial-core', choices=('MENU', 'PICO-8'), required=True)
    parser.add_argument('--host', default='192.168.1.176')
    args = parser.parse_args()
    args.evidence.mkdir()
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = json.loads((args.candidate / 'manifest.json').read_text())
    assert manifest['shared_Main_payload_included'] is False and manifest['expected_stock_Main_sha256'] == STOCK
    probe_receipt = json.loads((args.probe_build / 'result.json').read_text())
    assert probe_receipt['passed']
    for name, digest in probe_receipt['source_sha256'].items():
        assert sha(ROOT / name) == digest, name
    probe = args.probe_build / 'arm/tic80-hid-frontend-probe'
    assert sha(probe) == probe_receipt['ARM_probe_sha256']
    reader = json.loads((args.reader_evidence / 'result.json').read_text())
    assert reader['passed'] and reader['restored_verified'] and reader['original_probe_exit'] == 0
    assert reader['probe_sha256'] == manifest['probe_sha256']
    for path, item in manifest['files'].items():
        assert sha(args.candidate / 'candidate' / path) == item['candidate_sha256'], path
        assert sha(args.candidate / 'rollback' / path) == item['rollback_sha256'], path
    client = paramiko.SSHClient(); client.load_system_host_keys()
    test = PrivateTest(client, args.evidence); r = test.result
    ident = uuid.uuid4().hex
    remote = '/tmp/tic80-hid-api-' + ident
    stage = '/media/fat/.tic80-hid-api-' + ident
    r.update(started_at=datetime.now(timezone.utc).isoformat(), remote=remote, private_sd=stage,
             scope='Candidate player/Studio real mouse API, synthetic UHID mice, actual FPGA OSD epoch; stock Main')
    r['source_sha256'] = {name: sha(ROOT / name) for name in ('tools/test_hid_frontends_native.py', 'tools/hid_frontend_probe.c', 'tests/test_native_frontend_oracle.py')}
    r['candidate_payloads'] = manifest['files']
    r['probe_sha256'] = probe_receipt['ARM_probe_sha256']
    connected = switched = paused = False
    daemon = daemon_birth = restore = None
    current_pid = current_cart = current_frontend = current_mgl = None
    frontend_identity = expected = None
    active_probe = active_frontend = None
    caches = None
    allowed = ('', args.initial_core, 'MENU', 'TIC-80')
    protected = list(manifest['files']) + ['MiSTer', 'MiSTer.ini', 'MiSTer_Frontier/Master_Daemon.sh', 'linux/user-startup.sh']
    try:
        test.save(); connect(client, args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=25)
        connected = True
        assert test.core() == args.initial_core
        pid = test.main_pid(); argv = test.argv(pid)
        assert len(argv) >= 2
        restore = argv[1]
        assert restore.startswith('/media/fat/') and PurePosixPath(restore).suffix.lower() in ('.rbf', '.mgl')
        assert '..' not in PurePosixPath(restore).parts and not any(ord(c) < 32 for c in restore)
        r['restore_path'] = restore; r['restore_sha256'] = test.run('sha256sum ' + shlex.quote(restore)).split()[0]
        r['before'] = test.snapshot(protected)
        assert all(r['before'][path] == item['rollback_sha256'] for path, item in manifest['files'].items())
        r['filesystem'] = next(line.split()[2] for line in test.run('cat /proc/mounts').splitlines() if line.split()[1] == '/media/fat')
        r['carts_before'] = test.run("find /media/fat/games/TIC-80/Carts -exec stat -c '%d:%i:%s:%Y:%Z:%F %n' {} +")
        test.save()
        if args.initial_core != 'MENU':
            switched = True; test.load('/media/fat/menu.rbf', args.initial_core); test.wait_core('MENU', allowed)
        # Frontier gets one normal MENU transition to finish its old child.
        deadline = time.monotonic() + 30
        while True:
            candidates = []
            for entry in test.run('pidof bash sh || true').split():
                if not entry.isdigit():
                    continue
                words = test.argv(entry)
                if '/media/fat/MiSTer_Frontier/Master_Daemon.sh' in words:
                    candidates.append(entry)
            assert len(candidates) == 1, candidates
            daemon = candidates[0]
            if test.frontier_idle(daemon):
                break
            assert time.monotonic() < deadline, 'Frontier still has a child at MENU'
            time.sleep(.3)
        daemon_birth = test.birth(daemon); r['frontier_pid'] = daemon; r['frontier_birth'] = daemon_birth
        paused = True
        test.dispatch('pause-own-Frontier-at-MENU', 'test "$(cat /tmp/CORENAME)" = MENU && kill -STOP ' + daemon)
        assert '\nState:\tT' in '\n' + test.run('cat /proc/' + daemon + '/status')
        caches = {path: test.cache(path) for path in ('/media/fat/config/TIC-80.s0', '/media/fat/config/TIC80.s0')}
        r['source_caches_before'] = {path: None if data is None else data.hex() for path, data in caches.items()}
        test.dispatch('create-private-stages', 'test "$(cat /tmp/CORENAME)" = MENU && test ! -e ' + stage +
                      ' && test ! -e ' + remote + ' && mkdir ' + stage + ' ' + remote + ' ' + remote + '/projects ' + remote + '/saves')
        payloads = {'TIC80_20261003.rbf': '_Other/TIC80_20261003.rbf',
                    'TIC-80': 'games/TIC-80/TIC-80', 'TIC-80-Studio': 'games/TIC-80/TIC-80-Studio'}
        with client.open_sftp() as sftp:
            for name, path in payloads.items():
                sftp.put(str(args.candidate / 'candidate' / path), stage + '/' + name)
            sftp.put(str(probe), stage + '/probe')
        for name, path in payloads.items():
            assert test.run('sha256sum ' + stage + '/' + name).split()[0] == manifest['files'][path]['candidate_sha256']
        assert test.run('sha256sum ' + stage + '/probe').split()[0] == probe_receipt['ARM_probe_sha256']
        test.dispatch('private-executable-modes', 'chmod 755 ' + stage + '/TIC-80 ' + stage + '/TIC-80-Studio ' + stage + '/probe')
        for name in ('TIC-80', 'TIC-80-Studio', 'probe'):
            libraries = test.run('/lib/ld-linux-armhf.so.3 --list ' + stage + '/' + name)
            assert 'not found' not in libraries
            (args.evidence / (name + '-libraries.log')).write_text(libraries)
        for frontend in ('player', 'studio'):
            saveid = 'tic80-hid-api-' + ident + '-' + frontend
            cart = cartridge(saveid)
            cart_path = remote + '/' + frontend + '.tic'
            save_path = remote + '/saves/' + hashlib.md5(saveid.encode()).hexdigest() + '.pmem'
            mgl_path = remote + '/' + frontend + '.mgl'
            current_mgl = mgl_path
            mgl = ('<mistergamedescription>\n <rbf>.tic80-hid-api-' + ident + '/TIC80</rbf>\n' +
                   f' <file delay="3" type="f" index="0" path="{cart_path}"/>\n</mistergamedescription>\n')
            with client.open_sftp() as sftp:
                with sftp.open(cart_path, 'wb') as stream: stream.write(cart)
                with sftp.open(mgl_path, 'wb') as stream: stream.write(mgl.encode())
            switched = True; test.load(mgl_path, 'MENU'); test.wait_core('TIC-80', allowed)
            current_pid = test.main_pid()
            deadline = time.monotonic() + 30
            while True:
                words = test.run('test "$(cat /tmp/CORENAME)" = TIC-80 && for a in 0x3A00000C 0x3A000070 0x3A000074; do devmem "$a" 32 || exit 32; done').split()
                values = [int(word, 16) for word in words]
                if values[0] == 0x32495754 and values[1] & 3 == 2 and values[2] == len(cart):
                    current_cart = values[1:]; break
                assert time.monotonic() < deadline, values
                time.sleep(.2)
            r.setdefault('cycles', []).append({'frontend': frontend, 'main_pid': current_pid, 'cart': current_cart,
                                              'saveid': saveid, 'cart_sha256': hashlib.sha256(cart).hexdigest()})
            test.save()
            # Read automatic CTS repeatedly before launching the runtime.
            registers = '0x01 0x02 0x03 0x04 0x05 0x06 0x0a 0x3e 0x3f 0x42 0x55 0x9d 0x9e'
            clock = test.run('for n in 1 2; do for r in ' + registers + '; do printf "%s " "$r"; i2cget -y 1 0x39 "$r" b || exit 32; done; sleep .25; done').splitlines()
            assert len(clock) == 26
            r['cycles'][-1]['hdmi'] = analyze([{'registers': dict(line.split() for line in clock[n:n+13])} for n in (0, 13)])
            active_probe = remote + '/' + frontend + '-probe'
            active_frontend = remote + '/' + frontend + '-frontend'
            ready = active_probe + '.ready'; pid_path = active_frontend + '.pid'
            test.job(active_probe, 'taskset 1 nice -n 19 ' + stage + '/probe --frontend ' + current_pid + ' ' +
                     save_path + ' ' + ready + ' ' + pid_path + ' > ' + active_probe + '.jsonl 2> ' + active_probe + '.stderr')
            deadline = time.monotonic() + 25
            while not test.run('test -f ' + ready + ' && echo ready || true').strip():
                assert test.core() == 'TIC-80' and test.main_pid() == current_pid
                assert time.monotonic() < deadline, 'Original probe did not become ready'
                time.sleep(.2)
            program = stage + ('/TIC-80' if frontend == 'player' else '/TIC-80-Studio')
            expected = manifest['files']['games/TIC-80/TIC-80' + ('' if frontend == 'player' else '-Studio')]['candidate_sha256']
            arguments = (' --serve ' + remote + '/saves --ticks 7200') if frontend == 'player' else (
                ' --folder ' + remote + '/projects --saves ' + remote + '/saves --ticks 7200')
            # Match the installed handler: each production frontend selects
            # its own CPU and scheduling policy after startup.
            launch = (program + arguments + ' > ' + active_frontend + '.log 2>&1 &\n'
                      'child=$!\nprintf "%s\\n" "$child" > ' + pid_path + '\nwait "$child"')
            test.job(active_frontend, launch)
            deadline = time.monotonic() + 10
            while not test.run('test -f ' + pid_path + ' && cat ' + pid_path + ' || true').strip():
                assert time.monotonic() < deadline; time.sleep(.1)
            current_frontend = test.run('cat ' + pid_path).strip(); assert current_frontend.isdigit()
            frontend_identity = test.frontend_identity(current_frontend, expected)
            r['cycles'][-1]['frontend_identity'] = frontend_identity
            r['cycles'][-1]['frontend_pid'] = current_frontend; test.save()
            status = test.collect(active_probe, 180)
            stdout = test.run('cat ' + active_probe + '.jsonl'); stderr = test.run('cat ' + active_probe + '.stderr')
            (args.evidence / (frontend + '-probe.jsonl')).write_text(stdout)
            (args.evidence / (frontend + '-probe.stderr')).write_text(stderr)
            assert status == 0, stderr
            rows = validate_rows(stdout, current_pid, current_frontend)
            r['cycles'][-1]['api_passed'] = True; r['cycles'][-1]['probe_summary'] = rows[-1]; test.save()
            assert test.core() == 'TIC-80' and test.main_pid() == current_pid
            test.stop_frontend(frontend_identity, 'stop-own-' + frontend)
            assert test.collect(active_frontend, 20) == 0
            (args.evidence / (frontend + '-frontend.log')).write_text(test.run('cat ' + active_frontend + '.log'))
            current_frontend = frontend_identity = None; active_frontend = active_probe = None
            test.load('/media/fat/menu.rbf', 'TIC-80'); test.wait_core('MENU', allowed)
            current_pid = current_cart = None
        r['api_gate_passed'] = True
    except BaseException as error:
        r['error'] = type(error).__name__ + ': ' + str(error); test.save(); raise
    finally:
        try:
            if connected and 'before' in r:
                selected = test.core()
                assert selected in allowed, 'Another core selected; do not overwrite user selection'
                if selected == 'TIC-80':
                    settled = test.main_pid()
                    assert not current_pid or settled == current_pid, 'Main changed; preserve user selection'
                    assert current_mgl in test.argv(settled), 'Another MGL selected; preserve user selection'
                    if current_cart is not None:
                        words = test.run('for a in 0x3A000070 0x3A000074; do devmem "$a" 32 || exit 32; done').split()
                        assert [int(word,16) for word in words] == current_cart, 'Cart changed; preserve user selection'
                    test.load('/media/fat/menu.rbf', 'TIC-80'); test.wait_core('MENU', allowed)
                if active_probe:
                    test.collect(active_probe, 25)
                    for suffix in ('.jsonl', '.stderr'):
                        (args.evidence / (PurePosixPath(active_probe).name + suffix)).write_text(test.run('cat ' + active_probe + suffix))
                if active_frontend:
                    # Departure normally stops the parent; target only our
                    # hash-verified process if it has not yet exited.
                    status = test.run('if test -f ' + active_frontend + '.status; then cat ' + active_frontend + '.status; fi').strip()
                    if not status:
                        # A lost launch reply is reconciled through the original
                        # child PID file, never by dispatching another frontend.
                        if not current_frontend:
                            current_frontend = test.run('cat ' + active_frontend + '.pid').strip()
                        if test.argv(current_frontend):
                            if frontend_identity is None:
                                frontend_identity = test.frontend_identity(current_frontend, expected)
                            test.stop_frontend(frontend_identity, 'cleanup-own-frontend')
                    test.collect(active_frontend, 25)
                    (args.evidence / (PurePosixPath(active_frontend).name + '.log')).write_text(test.run('cat ' + active_frontend + '.log'))
                assert test.snapshot(protected) == r['before']
                carts = test.run("find /media/fat/games/TIC-80/Carts -exec stat -c '%d:%i:%s:%Y:%Z:%F %n' {} +")
                assert cartridge_metadata(carts,r['filesystem']) == cartridge_metadata(r['carts_before'],r['filesystem'])
                if caches is not None:
                    assert test.core() == 'MENU'
                    for path, original in caches.items():
                        observed = test.cache(path)
                        if observed != original:
                            assert observed is not None and remote.encode() + b'/' in observed, 'Another source cache changed; preserve it'
                            digest = hashlib.sha256(observed).hexdigest()
                            if original is None:
                                test.dispatch('remove-own-private-source-cache', 'test "$(sha256sum ' + path + ' | cut -d " " -f 1)" = ' + digest + ' && rm -- ' + path)
                            else:
                                with client.open_sftp() as sftp:
                                    with sftp.open(path, 'wb') as stream: stream.write(original)
                        assert test.cache(path) == original
                    r['source_caches_restored'] = True
                if paused:
                    assert test.birth(daemon) == daemon_birth and '/media/fat/MiSTer_Frontier/Master_Daemon.sh' in test.argv(daemon)
                    test.dispatch('resume-original-Frontier', 'kill -CONT ' + daemon)
                    paused = False
                if switched and test.core() == 'MENU':
                    assert test.run('sha256sum ' + shlex.quote(restore)).split()[0] == r['restore_sha256']
                    test.load(restore, 'MENU'); test.wait_core(args.initial_core, allowed)
                test.main_pid(); r['final_core'] = test.core(); assert r['final_core'] == args.initial_core
                assert test.snapshot(protected) == r['before']
                r['restored_verified'] = True; r['passed'] = bool(r.get('api_gate_passed'))
        except BaseException as error:
            r['restoration_error'] = type(error).__name__ + ': ' + str(error); test.save(); raise
        finally:
            if connected and paused:
                # Restore daemon operation even after a user core change.
                if test.birth(daemon) == daemon_birth and '/media/fat/MiSTer_Frontier/Master_Daemon.sh' in test.argv(daemon):
                    test.dispatch('emergency-resume-original-Frontier', 'kill -CONT ' + daemon)
            client.close(); test.save()
    print(json.dumps({'passed': r['passed'], 'restored_core': r['final_core'],
                      'installed_payloads_replaced': False, 'shared_Main_replaced': False}))


if __name__ == '__main__':
    main()
