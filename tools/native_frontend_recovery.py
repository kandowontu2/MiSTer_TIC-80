"""Actual stock-Main bad-cart, hung-tick and worker-death recovery checks.

The enclosing private frontend driver owns restoration and all process jobs.
Never signal Main, Frontier, an installed frontend, or an unverified worker.
"""
import hashlib
import json
import shlex
import struct
import time

from native_frontend_lifecycle import pmem


def cartridge(saveid, body):
    code = ('-- script: lua\n-- saveid: ' + saveid + '\n' + body).encode()
    assert len(code) < 65536
    return bytes([17, 0, 0, 0, 5]) + struct.pack('<H', len(code)) + b'\0' + code


def ordinary(saveid):
    return cartridge(saveid, 'function BOOT() pmem(1,pmem(1)+1) end\n'
                     'function TIC() pmem(0,pmem(0)+1); cls(9) end\n')


def validate_checkpoint(values, *, minimum_ticks=0, boots=None, last_complete=None):
    assert len(values) == 256
    assert values[0] >= minimum_ticks, ('Game did not advance', values[:2])
    if boots is not None:
        assert values[1] == boots, ('Unexpected BOOT count', values[:2], boots)
    if last_complete is not None:
        assert values[0] == last_complete, ('Incomplete tick reached persistent memory', values[0])


def delivered(ticket, size, ack, source, expected_size):
    # The real cart loader publishes state 3 for fewer than four bytes;
    # a complete malformed envelope gets state 2 and is rejected by the VM.
    expected_state = 3 if expected_size < 4 else 2
    if ticket & 3 != expected_state or ack != ticket or size != expected_size:
        return False
    assert source == 0
    return True


def run_recovery(test, args, stage, remote, label, identity, frontend, update):
    rows = []
    logpath = remote + '/' + label + '-frontend.log'

    def guard():
        assert test.core() == 'TIC-80'
        assert test.frontend_identity(identity['pid'], identity['sha256']) == identity

    def wait(predicate, reason, seconds=25):
        deadline = time.monotonic() + seconds
        while True:
            guard()
            result = predicate()
            if result:
                return result
            assert time.monotonic() < deadline, (reason, test.run('tail -35 ' + logpath))
            time.sleep(.2)

    def values(path):
        try:
            return pmem(test, path)
        except FileNotFoundError:
            return None

    def checkpoint(path, minimum_ticks=0, boots=None, last_complete=None):
        def ready():
            value = values(path)
            if value is None or value[0] < minimum_ticks:
                return None
            if last_complete is not None and value[0] < last_complete:
                return None
            validate_checkpoint(value, minimum_ticks=minimum_ticks, boots=boots,
                                last_complete=last_complete)
            return value
        return wait(ready, 'Expected persistent checkpoint did not arrive')

    def save():
        test.result['cycles'][-1]['recovery_actions'] = rows
        test.save()

    def load(name, payload, extension='tic'):
        base = remote + '/' + label + '-recovery-' + name
        mgl = ('<mistergamedescription>\n <rbf>' + stage.removeprefix('/media/fat/') + '/TIC80</rbf>\n'
               f' <file delay="3" type="f" index="0" path="{base}.{extension}"/>\n'
               '</mistergamedescription>\n').encode()
        with test.client.open_sftp() as sftp:
            with sftp.open(base + '.' + extension, 'wb') as stream:
                stream.write(payload)
            with sftp.open(base + '.mgl', 'wb') as stream:
                stream.write(mgl)
        (args.evidence / (label + '-recovery-' + name + '.' + extension)).write_bytes(payload)
        before = test.run('cat ' + logpath)
        update(base + '.mgl')
        test.load(base + '.mgl', 'TIC-80')
        test.wait_core('TIC-80', ('', 'TIC-80'))
        def acknowledged():
            words = test.run('for a in 0x3A000070 0x3A000074 0x3A000038 0x3A04C204; do devmem "$a" 32 || exit 32; done').split()
            ticket, size, ack, source = [int(word, 16) for word in words]
            if not delivered(ticket, size, ack, source, len(payload)):
                return None
            main = test.main_pid()
            assert base + '.mgl' in test.argv(main)
            return dict(main_pid=main, cart=[ticket, size], ticket=ticket, ack=ack,
                        source_length=source, path=base + '.mgl')
        delivery = wait(acknowledged, 'Original bad/good cartridge transfer was not acknowledged')
        return before, delivery

    def monitor(name):
        prefix = remote + '/' + label + '-recovery-' + name + '-clock'
        test.job(prefix, 'taskset 1 nice -n 19 ' + stage + '/monitor --seconds 2 --interval-ms 13 > ' + prefix + '.jsonl 2> ' + prefix + '.stderr')
        status = test.collect(prefix, 10)
        raw = test.run('cat ' + prefix + '.jsonl')
        errors = test.run('cat ' + prefix + '.stderr')
        (args.evidence / (label + '-recovery-' + name + '-clock.jsonl')).write_text(raw, encoding='utf-8')
        (args.evidence / (label + '-recovery-' + name + '-clock.stderr')).write_text(errors, encoding='utf-8')
        samples = [json.loads(line) for line in raw.splitlines()]
        assert status == 0 and len(samples) >= 100, errors
        assert len({x['session'] for x in samples}) == 1
        assert all(x['underruns'] == 0 for x in samples)
        assert ((samples[-1]['played'] - samples[0]['played']) & 0xffffffff) > 90000
        assert (((samples[-1]['presented'] >> 2) - (samples[0]['presented'] >> 2)) & 0x3fffffff) >= 90
        return dict(audio_samples=len(samples), session=samples[0]['session'], underruns=0)

    marker = 'Cartridge loaded:' if frontend == 'player' else 'MiSTer cartridge running:'
    rejected = 'Cartridge rejected:' if frontend == 'player' else 'MiSTer cartridge rejected;'
    wait(lambda: marker in test.run('cat ' + logpath), 'Initial private warm cartridge did not run')
    def new_log(marker_, previous):
        def ready():
            current = test.run('cat ' + logpath)
            return current if current.count(marker_) > previous.count(marker_) else None
        return wait(ready, 'Expected frontend recovery log marker missing')

    def key(name):
        return 'tic80-recovery-' + hashlib.md5((remote + label + name).encode()).hexdigest()

    def path(saveid):
        return remote + '/saves/' + hashlib.md5(saveid.encode()).hexdigest() + '.pmem'

    good = key('baseline')
    before, last = load('baseline', ordinary(good))
    new_log(marker, before)
    current = checkpoint(path(good), 120, 1)
    rows.append(dict(action='baseline', ticks=current[0], boots=1, save_path=path(good), **last, **monitor('baseline')))
    save()
    for name, payload, extension in (
        ('empty', b'', 'tic'),
        ('invalid-png', b'\x89PNG\r\n\x1a\ninvalid', 'png'),
        ('runaway-first-tick', cartridge(key('runaway'), 'function TIC() while true do end end\n'), 'tic'),
    ):
        previous = checkpoint(path(good), 120)
        before, last = load(name, payload, extension)
        resumed = None
        if frontend == 'studio' and name == 'runaway-first-tick':
            # Studio has already selected the code before Run starts. A bad
            # first tick returns its worker to the editor for repair, rather
            # than rejecting the selection as the cartridge-only player does.
            new_log('Studio worker recovered:', before)
            before_resume, resumed = load(name + '-resume', ordinary(good))
            new_log(marker, before_resume)
        else:
            rejection_marker = ('MiSTer cartridge transfer rejected;' if name == 'empty' else rejected)
            new_log(rejection_marker if frontend == 'studio' else rejected, before)
            if frontend == 'studio' and name == 'invalid-png':
                # The rejected selection retains the old editor contents.
                # Explicit Run must resume those exact contents successfully.
                prefix = remote + '/' + label + '-recovery-run-retained'
                test.job(prefix, stage + '/keyboard run > ' + prefix + '.log 2>&1')
                assert test.collect(prefix, 15) == 0, test.run('cat ' + prefix + '.log')
        expected_boots = previous[1] + (1 if frontend == 'studio' else 0)
        current = checkpoint(path(good), previous[0] + 120, expected_boots)
        rows.append(dict(action=name, rejected=not (frontend == 'studio' and name == 'runaway-first-tick'),
                         editor_worker_recovered=frontend == 'studio' and name == 'runaway-first-tick',
                         explicit_Run=frontend == 'studio' and name == 'invalid-png', resumed_delivery=resumed,
                         previous_ticks=previous[0],
                         ticks=current[0], boots=current[1], save_path=path(good), **last, **monitor(name)))
        save()
        print(frontend + '/' + name + ': recovery verified; previous cartridge advances with coherent save', flush=True)

    late = key('late-hang')
    payload = cartridge(late, 'n=0\nfunction TIC() n=n+1; pmem(0,n); cls(5); '
                        'if n==3 then pmem(0,9999); while true do end end end\n')
    before, last = load('late-hang', payload)
    checkpoint(path(late), last_complete=2)
    # The incomplete tick must remain excluded after the failure has settled.
    time.sleep(.5)
    current = checkpoint(path(late), last_complete=2)
    rows.append(dict(action='late-hang', last_completed_tick=current[0], save_path=path(late), **last))
    save()
    print(frontend + '/late-hang: incomplete tick excluded from save', flush=True)

    recovered = key('after-hang')
    before, last = load('after-hang', ordinary(recovered))
    new_log(marker, before)
    current = checkpoint(path(recovered), 120, 1)
    rows.append(dict(action='after-hang', ticks=current[0], boots=1, save_path=path(recovered), **last, **monitor('after-hang')))
    save()

    listing = test.run('ps -eo pid,ppid,comm').splitlines()
    expected_comm = 'tic80-vm' if frontend == 'player' else 'tic80-studio'
    workers = [row.split()[0] for row in listing if len(row.split()) == 3 and
               row.split()[1] == identity['pid'] and row.split()[2] == expected_comm]
    assert len(workers) == 1, workers
    worker = workers[0]
    worker_identity = test.frontend_identity(worker, identity['sha256'])
    assert ('--vm-worker' if frontend == 'player' else '--studio-worker') in test.argv(worker)
    guard()
    prefix = remote + '/' + label + '-recovery-kill-worker'
    command = ('test "$(cat /tmp/CORENAME)" = TIC-80 && '
               'test "$(sha256sum /proc/' + worker + '/exe | cut -d " " -f 1)" = ' + identity['sha256'] + ' && '
               'test "$(sed "s/.*) //" /proc/' + worker + '/stat | awk \'{print $20}\')" = ' + worker_identity['birth'] + ' && '
               'test "$(sed "s/.*) //" /proc/' + worker + '/stat | awk \'{print $2}\')" = ' + identity['pid'] + ' && '
               'kill -KILL ' + worker)
    test.job(prefix, command)
    assert test.collect(prefix, 10) == 0
    wait(lambda: not test.run('test -e /proc/' + worker + ' && echo present || true').strip(),
         'Killed original worker was not reaped')
    rows.append(dict(action='kill-worker', worker_identity=worker_identity, parent_retained=True))
    save()
    final = key('after-kill')
    before, last = load('after-kill', ordinary(final))
    new_log(marker, before)
    current = checkpoint(path(final), 120, 1)
    rows.append(dict(action='after-kill', ticks=current[0], boots=1, save_path=path(final), **last, **monitor('after-kill')))
    assert not test.run('find ' + shlex.quote(remote + '/saves') + " -name '*.tmp-*' -print").strip()
    save()
    print(frontend + '/after-kill: new cartridge runs; original parent retained', flush=True)
    return dict(passed=True, actions=rows, last=last, original_frontend_retained=True)
