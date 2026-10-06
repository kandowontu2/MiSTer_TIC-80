"""Own one physical-input test window and restore the installed Studio.

Requires a fresh, clean inspect_studio_native receipt and connected devices
for the selected check. The underlying diagnostic never sends synthetic input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import paramiko
from hardware_ssh import connect
from inspect_studio_native import classify_studio_processes
from test_hid_frontends_native import PrivateTest

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'fixture', 'inspect-receipt', 'probe-build',
                 'reader-evidence', 'evidence', 'native-evidence'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--host', default='192.168.1.176')
    p.add_argument('--seconds', type=int, default=600)
    p.add_argument('--kind', choices=('controller-mouse', 'keyboard'), default='controller-mouse')
    a = p.parse_args()
    assert 10 <= a.seconds <= 3600
    assert not a.native_evidence.exists()
    before = json.loads(a.inspect_receipt.read_text(encoding='utf-8'))
    assert before['passed'] and before['state']['mode'] == 1 and before['state']['home'] == 1
    assert before['state']['modified'] == before['state']['selecting'] == 0
    expected = before['expected_Studio_sha256']
    manifest = json.loads((a.candidate / 'manifest.json').read_text(encoding='utf-8'))
    assert manifest['files']['games/TIC-80/TIC-80-Studio']['rollback_sha256'] == expected
    a.evidence.mkdir()
    c = paramiko.SSHClient(); c.load_system_host_keys()
    def reconnect():
        c.close(); connect(c, a.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=25)
    test = PrivateTest(c, a.evidence, reconnect=reconnect); r = test.result
    protected = list(before['before'])
    changed = False
    try:
        reconnect()
        assert test.core() == 'TIC-80' and test.main_pid() == before['Main_pid']
        assert test.birth(before['Main_pid']) == before['Main_birth']
        assert test.snapshot(protected) == before['before']
        devices = test.run('cat /proc/bus/input/devices')
        r['physical_input_inventory'] = devices; test.save()
        # Main's virtual keyboard must never satisfy this readiness gate.
        blocks = [block for block in devices.split('\n\n') if 'Name="MiSTer virtual input"' not in block]
        if a.kind == 'keyboard':
            assert any('Handlers=' in b and 'kbd' in b.split('Handlers=', 1)[1].split('\n', 1)[0] for b in blocks), 'Connect or wake the physical keyboard'
        else:
            assert any('Handlers=' in b and 'js' in b.split('Handlers=', 1)[1].split('\n', 1)[0] for b in blocks), 'Connect the physical gamepad'
            assert any('Handlers=' in b and 'mouse' in b.split('Handlers=', 1)[1].split('\n', 1)[0] for b in blocks), 'Connect the physical mouse'
        parent = [pid for pid in before['frontend_identities'] if '--studio-worker' not in test.argv(pid)]
        assert len(parent) == 1
        for pid, identity in before['frontend_identities'].items():
            assert test.frontend_identity(pid, identity['sha256']) == dict(pid=pid, **identity)
        reader = before['remote'] + '/reader'
        assert test.run('sha256sum ' + reader).split()[0] == before['binary_sha256']
        links = test.run('for f in /proc/' + parent[0] + '/fd/*; do printf \'%s\\t\' "$f"; readlink "$f"; done')
        ipc = [line.split('\t')[0] for line in links.splitlines() if line.endswith('/memfd:tic80-studio (deleted)')]
        assert len(ipc) == 1
        guard = 'test "$(cat /tmp/CORENAME)" = TIC-80 && '
        for pid, identity in before['frontend_identities'].items():
            guard += 'test "$(sed "s/.*) //" /proc/' + pid + '/stat | awk \'{print $20}\')" = ' + identity['birth'] + ' && '
        guard += 'case "$(' + reader + ' ' + ipc[0] + ')" in *"mode=1 home=1 modified=0 selecting=0"*) ;; *) exit 44;; esac\n'
        guard += "printf %s 'load_core /media/fat/menu.rbf\n' > /dev/MiSTer_cmd"
        r.update(scope=__doc__, physical_kind=a.kind, before=before['before'],
                 source_guard_receipt_sha256=hashlib.sha256(a.inspect_receipt.read_bytes()).hexdigest(),
                 input_injected=False)
        test.save()
        changed = True
        job = before['remote'] + '/original-physical-' + a.kind + '-clean-Studio-to-MENU'
        test.job(job, guard); assert test.collect(job, 10) == 0
        test.wait_core('MENU', ('', 'TIC-80', 'MENU'))
        cmd = [sys.executable, str(ROOT / 'tools/test_hid_frontends_native.py'),
               '--candidate', str(a.candidate), '--probe-build', str(a.probe_build),
               '--reader-evidence', str(a.reader_evidence), '--evidence', str(a.native_evidence),
               '--initial-core', 'MENU', '--host', a.host, '--music-fixtures', str(a.fixture),
               '--frontends', 'player', '--physical-keyboard' if a.kind == 'keyboard' else '--physical-input',
               '--soak-seconds', str(a.seconds)]
        with (a.evidence / 'original-driver.log').open('x', encoding='utf-8') as log:
            child = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
            r.update(original_driver_pid=child.pid, original_driver_command=cmd); test.save()
            code = child.wait()
        r['original_driver_exit'] = code; test.save()
        assert code == 0, 'Inspect original-driver.log; do not restart this job'
        receipt = json.loads((a.native_evidence / 'result.json').read_text(encoding='utf-8'))
        assert receipt['passed'] and receipt['restored_verified'] and receipt['physical_gate_passed']
        r['native_physical_receipt_sha256'] = hashlib.sha256((a.native_evidence / 'result.json').read_bytes()).hexdigest()
        r['passed'] = True; test.save()
    except BaseException as error:
        r['error'] = type(error).__name__ + ': ' + str(error); test.save(); raise
    finally:
        try:
            if changed:
                assert test.core() == 'MENU', 'Preserve the original job and unexpected user core selection'
                assert test.snapshot(protected) == before['before']
                test.load('/media/fat/_Other/TIC80_20261003.rbf', 'MENU')
                test.wait_core('TIC-80', ('', 'MENU', 'TIC-80'))
                deadline = time.monotonic() + 20
                while True:
                    processes = {pid: test.argv(pid) for pid in test.run('pidof TIC-80-Studio || true').split()}
                    try:
                        parent, worker, auxiliary = classify_studio_processes(processes)
                        break
                    except AssertionError:
                        assert time.monotonic() < deadline; time.sleep(.2)
                identities = {pid: test.frontend_identity(pid, expected) for pid in (parent, worker, *auxiliary)}
                assert test.snapshot(protected) == before['before']
                r.update(installed_Studio_restored=True, final_core=test.core(),
                         final_Main_pid=test.main_pid(), final_Studio_identities=identities)
                test.save(); print(dict(physical_passed=r.get('passed'), Studio_restored=True), flush=True)
        finally:
            c.close()


if __name__ == '__main__': main()
