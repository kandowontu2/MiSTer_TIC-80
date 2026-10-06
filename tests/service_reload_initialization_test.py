"""A new FPGA session can precede Main's later reset assertion and release."""
import argparse
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from service_recovery_test import Fixture, cartridge, save_values


def run(service, mode):
    with tempfile.TemporaryDirectory(prefix='tic80-main-init-') as directory:
        root = Path(directory)
        fixture = Fixture(root)
        selected = root / 'CORENAME'
        selected.write_text('TIC-80')
        key = 'main-initialization'
        payload = cartridge(key, 'function BOOT() pmem(1,pmem(1)+1) end\n'
                            'function TIC() pmem(0,pmem(0)+1); cls(9); '
                            'trace("completed-tick:"..pmem(0)) end\n')
        fixture.actions = [(5, lambda: fixture.transfer(6, payload))]
        try:
            with (root / 'stdout').open('w+b') as out, (root / 'stderr').open('w+b') as err:
                fixture.thread.start()
                process = fixture.process = subprocess.Popen(
                    [service, '--serve', str(fixture.saves), '--memory', str(fixture.memory),
                     '--core-name', str(selected)], stdout=out, stderr=err)

                def wait(predicate, seconds=3):
                    deadline = time.monotonic() + seconds
                    while not predicate():
                        assert process.poll() is None, (root / 'stderr').read_text()
                        assert time.monotonic() < deadline, (root / 'stdout').read_text()
                        time.sleep(.005)

                def saved(boots, ticks):
                    try:
                        data = save_values(fixture.saves, key)
                        return data[1] >= boots and data[0] >= ticks
                    except FileNotFoundError:
                        return False

                wait(lambda: saved(1, 60))
                before = save_values(fixture.saves, key)
                if mode == 'prior-reset':
                    fixture.put('STATUS', 1)
                    at = len(fixture.frames)
                    wait(lambda: len(fixture.frames) >= at + 8)
                    # The old FPGA's held reset disappears at reconfiguration;
                    # this falling edge does not complete the new initialization.
                    fixture.put('STATUS', 0)
                # Configuration establishes fresh FPGA identity/session before
                # Main enters user_io_init and asserts its status reset bit.
                at = len(fixture.frames)
                fixture.put('SESSION_ACK', 0)
                wait(lambda: len(fixture.frames) >= at + 8)
                early = (root / 'stdout').read_text()
                held_ticks = [int(line.split(':')[1]) for line in (root / 'stderr').read_text().splitlines()
                              if line.startswith('completed-tick:')][-1]
                if mode == 'departure':
                    selected.write_text('MENU')
                    process.wait(timeout=2)
                    final = save_values(fixture.saves, key)
                    output = (root / 'stdout').read_text()
                    assert process.returncode == 0, (root / 'stderr').read_text()
                    assert final[1] == 1 and final[0] == held_ticks, final[:2]
                    assert 'Cartridge restarted:' not in output, output
                    assert 'Core switched; runtime stopped' in output, output
                    print('Departure during initialization: no extra BOOT, last completed save, clean exit')
                    return
                if mode != 'generation-only':
                    fixture.put('STATUS', 1)
                    at = len(fixture.frames)
                    wait(lambda: len(fixture.frames) >= at + 8)
                    fixture.put('STATUS', 0)
                if mode != 'release-only':
                    # Main's SetUARTMode rewrites CORENAME after releasing reset.
                    # The service can miss the short reset pulse altogether.
                    selected.write_text('TIC-80')
                wait(lambda: saved(2, before[0] + 60))
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=2)
                output = (root / 'stdout').read_text()
                assert process.returncode == 0, (root / 'stderr').read_text()
                final = save_values(fixture.saves, key)
                print(mode, 'new session before Main initialization:', final[:2], 'early restarts:', early.count('Cartridge restarted:'), flush=True)
                assert final[1] == 2, (final[:2], output)
                assert 'Cartridge restarted:' not in early, early
                assert output.count('Cartridge restarted:') == 1, output
                assert not list(fixture.saves.glob('*.tmp-*'))
                print('Cached VM held until initialization, one restart, BOOT=2, CRC-valid final save')
        finally:
            fixture.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--service', required=True)
    parser.add_argument('--mode', choices=['delayed-reset', 'release-only', 'generation-only', 'departure', 'prior-reset'])
    args = parser.parse_args()
    service = args.service
    for mode in ([args.mode] if args.mode else ['delayed-reset', 'release-only', 'generation-only', 'departure', 'prior-reset']):
        run(service, mode)
