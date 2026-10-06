"""Recover autosaves after one injected storage failure without resetting the VM."""
import argparse
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import time
from service_recovery_test import Fixture, ordinary, save_values


def run(service, library):
    with tempfile.TemporaryDirectory(prefix='tic80-save-recovery-') as directory:
        root = Path(directory)
        fixture = Fixture(root)
        # The loader splits LD_PRELOAD on spaces. Use this fixture's space-free
        # temporary path even when the source workspace contains spaces.
        preload = root / 'fault.so'
        shutil.copyfile(library, preload)
        key = 'service-save-recovery'
        fixture.actions = [(5, lambda: fixture.transfer(6, ordinary(key, 9)))]
        try:
            with (root / 'stdout').open('w+b') as out, (root / 'stderr').open('w+b') as err:
                fixture.thread.start()
                env = os.environ.copy()
                env['LD_PRELOAD'] = str(preload)
                # The intentional fsync shim precedes libasan in an instrumented
                # build. Permit that load order; keep address checks enabled.
                env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':verify_asan_link_order=0'
                env['TM_TEST_PMEM_FAIL_ONCE_DIR'] = str(fixture.saves) + '/'
                process = fixture.process = subprocess.Popen(
                    [service, '--serve', str(fixture.saves), '--memory', str(fixture.memory)],
                    stdout=out, stderr=err, env=env)
                deadline = time.monotonic() + 9
                worker = None
                while True:
                    errors = (root / 'stderr').read_text()
                    assert 'cannot be preloaded' not in errors, errors
                    assert process.poll() is None, errors
                    assert time.monotonic() < deadline, (errors, (root / 'stdout').read_text())
                    children = Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
                    if children:
                        assert len(children) == 1
                        if worker is None:
                            worker = children[0]
                        assert children == [worker], 'Transient save failure replaced the cartridge VM'
                    try:
                        saved = save_values(fixture.saves, key)
                    except FileNotFoundError:
                        saved = None
                    if saved and saved[0] >= 180:
                        break
                    time.sleep(.01)
                assert saved[1] == 1 and saved[0] >= 180
                assert errors.count('Injected one persistent-memory fsync failure') == 1, errors
                assert errors.count('Persistent memory save: Input/output error') == 1, errors
                assert len(fixture.frames) > saved[0] + 100, 'Save-error notice/pause was not exercised'
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=2)
                assert process.returncode == 0, (root / 'stderr').read_text()
                final = save_values(fixture.saves, key)
                assert final[0] >= saved[0] and final[1] == 1
                output = (root / 'stdout').read_text()
                assert output.count('Cartridge loaded:') == 1 and 'Cartridge restarted:' not in output
                assert not Path('/proc/' + worker).exists()
                assert not list(fixture.saves.glob('*.tmp-*'))
                print('Service autosave recovery: one I/O failure, notice pause, same VM/BOOT=1, later CRC-valid autosaves and final flush')
        finally:
            fixture.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--service', required=True)
    parser.add_argument('--fault-library', required=True)
    args = parser.parse_args()
    run(args.service, args.fault_library)
