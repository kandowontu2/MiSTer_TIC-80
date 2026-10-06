"""A cartridge delivered during held reset loads once after reset release."""
import argparse
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from service_recovery_test import Fixture, ordinary, save_values


def run(service):
    with tempfile.TemporaryDirectory(prefix='tic80-held-transfer-') as directory:
        root = Path(directory)
        fixture = Fixture(root)
        fixture.actions = [(5, lambda: fixture.transfer(6, ordinary('held-old', 2)))]
        try:
            with (root / 'stdout').open('w+b') as out, (root / 'stderr').open('w+b') as err:
                fixture.thread.start()
                process = fixture.process = subprocess.Popen(
                    [service, '--serve', str(fixture.saves), '--memory', str(fixture.memory)],
                    stdout=out, stderr=err)

                def wait(predicate, seconds=3):
                    deadline = time.monotonic() + seconds
                    while not predicate():
                        assert process.poll() is None, (root / 'stderr').read_text()
                        assert time.monotonic() < deadline, (root / 'stdout').read_text()
                        time.sleep(.005)

                def saved(key):
                    try:
                        return save_values(fixture.saves, key)[0] >= 60
                    except FileNotFoundError:
                        return False

                wait(lambda: saved('held-old'))
                old_ticks = save_values(fixture.saves, 'held-old')[0]
                children = Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
                assert len(children) == 1
                fixture.put('STATUS', 1)
                held_at = len(fixture.frames)
                wait(lambda: len(fixture.frames) >= held_at + 8)
                fixture.transfer(10, ordinary('held-new', 9))
                held_save = next(fixture.saves.glob('*.pmem')).read_bytes()
                held_at = len(fixture.frames)
                wait(lambda: len(fixture.frames) >= held_at + 65)
                assert fixture.get('CART_ACK') == 6
                assert next(fixture.saves.glob('*.pmem')).read_bytes() == held_save
                assert Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split() == children
                assert 'Cartridge restarted:' not in (root / 'stdout').read_text()
                fixture.put('STATUS', 0)
                wait(lambda: fixture.get('CART_ACK') == 10)
                wait(lambda: saved('held-new'))
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=2)
                assert process.returncode == 0, (root / 'stderr').read_text()
                assert save_values(fixture.saves, 'held-old')[1] == 1
                assert 0 <= save_values(fixture.saves, 'held-old')[0] - old_ticks <= 8
                assert save_values(fixture.saves, 'held-new')[1] == 1
                output = (root / 'stdout').read_text()
                assert output.count('Cartridge loaded:') == 2 and 'Cartridge restarted:' not in output
                assert not list(fixture.saves.glob('*.tmp-*'))
                assert all(not Path('/proc/' + child).exists() for child in children)
                print('Held reset: pending transfer retained, no VM ticks/autosaves/reboot while held, one load on release, CRC-valid saves')
        finally:
            fixture.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--service', required=True)
    run(parser.parse_args().service)
