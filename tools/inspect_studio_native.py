"""Read coherent installed-Studio state without switching cores or sending keys."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import uuid
import zipfile
import paramiko
from hardware_ssh import connect
from test_hid_frontends_native import PrivateTest

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = '/media/fat/games/TIC-80/TIC-80-Studio'


def classify_studio_processes(processes):
    parents = [pid for pid, argv in processes.items() if argv and argv[0] == PROGRAM]
    assert len(parents) == 1, ('Expected one normal installed Studio parent', parents)
    parent = parents[0]
    workers, auxiliary = [], []
    for pid, argv in processes.items():
        if pid == parent:
            continue
        if argv == ['tic80-studio', '--studio-worker', parent]:
            workers.append(pid)
        elif argv == ['tic80-hid-wheel', '--hid-wheel-worker', '/dev', parent]:
            auxiliary.append(pid)
        else:
            raise AssertionError(('Unexpected Studio executable process', pid, argv))
    assert len(workers) == 1 and len(auxiliary) <= 1, (workers, auxiliary)
    return parent, workers[0], auxiliary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.1.176')
    p.add_argument('--reader-build', type=Path, required=True)
    p.add_argument('--expected-studio-sha256', required=True)
    p.add_argument('--evidence', type=Path, required=True)
    a = p.parse_args()
    assert re.fullmatch('[0-9a-f]{64}', a.expected_studio_sha256)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    reader = json.loads((a.reader_build / 'result.json').read_text(encoding='utf-8'))
    assert reader['passed'] and reader['read_only']
    assert reader['ABI_source_sha256'] == sha(ROOT / 'src/studio_session.c')
    assert reader['binary_sha256'] == sha(a.reader_build / 'inspect')
    old_source = ROOT / 'releases/TIC80-Corresponding-Source-v0.1.0-dev.20261005.zip'
    assert sha(old_source) == reader['published_source_sha256']
    with zipfile.ZipFile(old_source) as z:
        for name in ('src/studio_session.c', 'include/tic80_mister/studio_session.h',
                     'include/tic80_mister/fft.h', 'include/tic80_mister/history_private.h'):
            assert z.read(name) == (ROOT / name).read_bytes(), name
    a.evidence.mkdir()
    c = paramiko.SSHClient(); c.load_system_host_keys()
    def reconnect():
        c.close(); connect(c, a.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=25)
    test = PrivateTest(c, a.evidence, reconnect=reconnect); r = test.result
    remote = '/tmp/tic80-studio-inspect-' + uuid.uuid4().hex
    protected = ['MiSTer', 'MiSTer.ini', '_Other/TIC80_20261003.rbf',
                 'games/TIC-80/TIC-80', 'games/TIC-80/TIC-80-Studio', 'games/TIC-80/_handler.sh',
                 'games/TIC-80/cacert.pem', 'MiSTer_Frontier/Master_Daemon.sh', 'linux/user-startup.sh',
                 'Scripts/Install_TIC80.sh', 'Scripts/TIC80-install/files.sha256',
                 'Scripts/TIC80-install/frontier.sha256']
    try:
        reconnect(); assert test.core() == 'TIC-80'
        main = test.main_pid(); birth = test.birth(main); argv = test.argv(main)
        assert argv == ['/media/fat/MiSTer', '/media/fat/_Other/TIC80_20261003.rbf']
        processes = {pid: test.argv(pid) for pid in test.run('pidof TIC-80-Studio').split()}
        parent, worker, auxiliary = classify_studio_processes(processes)
        identities = {pid: {key: value for key, value in test.frontend_identity(pid, a.expected_studio_sha256).items() if key != 'pid'} for pid in (parent, worker)}
        for pid in auxiliary:
            test.frontend_identity(pid, a.expected_studio_sha256)
        r.update(scope='Read-only ABI-bound current installed Studio; classify VM and optional HID-wheel child explicitly',
                 expected_Studio_sha256=a.expected_studio_sha256, Main_pid=main, Main_argv=argv, Main_birth=birth,
                 frontend_identities=identities, auxiliary_processes={pid: processes[pid] for pid in auxiliary},
                 before=test.snapshot(protected), binary_sha256=reader['binary_sha256'], remote=remote,
                 ABI_source_sha256=reader['ABI_source_sha256'], source_sha256=sha(Path(__file__)))
        test.save()
        test.dispatch('create-private-reader-stage', 'test ! -e ' + remote + ' && mkdir ' + remote)
        with c.open_sftp() as sftp:
            sftp.put(str(a.reader_build / 'inspect'), remote + '/reader')
        assert test.run('sha256sum ' + remote + '/reader').split()[0] == reader['binary_sha256']
        test.dispatch('private-reader-mode', 'chmod 755 ' + remote + '/reader')
        links = test.run('for f in /proc/' + parent + '/fd/*; do printf \'%s\\t\' "$f"; readlink "$f"; done')
        ipc = [line.split('\t')[0] for line in links.splitlines() if line.endswith('/memfd:tic80-studio (deleted)')]
        assert len(ipc) == 1
        job = remote + '/original-current-Studio-state'
        test.job(job, 'taskset -c 1 nice -n 19 ' + remote + '/reader ' + ipc[0] + ' > ' + job + '.stdout 2> ' + job + '.stderr')
        status = test.collect(job, 20)
        stdout, stderr = [test.run('cat ' + job + suffix) for suffix in ('.stdout', '.stderr')]
        (a.evidence / 'original.stdout').write_text(stdout, encoding='utf-8')
        (a.evidence / 'original.stderr').write_text(stderr, encoding='utf-8')
        fields = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', stdout)}
        assert status == 0 and set(fields) == {'sequence', 'mode', 'home', 'modified', 'selecting', 'ipc_bytes'}, (status, stdout, stderr)
        assert test.core() == 'TIC-80' and test.main_pid() == main and test.birth(main) == birth
        for pid, identity in identities.items():
            assert test.frontend_identity(pid, identity['sha256']) == dict(pid=pid, **identity)
        assert test.snapshot(protected) == r['before']
        r.update(passed=True, state=fields, original_status=status, installed_Studio_retained=True, core_switched=False)
        test.save(); print(dict(passed=True, state=fields, Main=main, Studio=[parent, worker], auxiliary=auxiliary))
    except BaseException as error:
        r['error'] = type(error).__name__ + ': ' + str(error); test.save(); raise
    finally:
        c.close(); test.save()


if __name__ == '__main__':
    main()
