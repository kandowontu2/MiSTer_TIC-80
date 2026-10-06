"""Real service/worker exit checks with selected-core and DDR fault fixtures."""
import argparse
from pathlib import Path
import signal
import subprocess
import tempfile
import threading
import time
from service_recovery_test import Fixture, ordinary, save_values


def select(path, name):
    staged = path.with_suffix('.next')
    staged.write_text(name)
    staged.replace(path)


def run(service, scenario):
    with tempfile.TemporaryDirectory(prefix='tic80-lifecycle-') as directory:
        root = Path(directory)
        fixture = Fixture(root)
        selected = root/'CORENAME'
        select(selected, 'TIC-80')
        key = 'lifecycle-'+scenario
        fixture.actions = [(5, lambda: fixture.transfer(6, ordinary(key, 2)))]
        try:
            with (root/'stdout').open('w+b') as out, (root/'stderr').open('w+b') as err:
                fixture.thread.start()
                process = fixture.process = subprocess.Popen([service, '--serve', str(fixture.saves),
                    '--memory', str(fixture.memory), '--core-name', str(selected)], stdout=out, stderr=err)
                # Complete a real autosave before introducing a departure/fault.
                deadline = time.monotonic()+6
                while True:
                    assert process.poll() is None, (root/'stderr').read_text()
                    try: before = save_values(fixture.saves, key)
                    except FileNotFoundError: before = None
                    if before and before[0]>=60: break
                    assert time.monotonic()<deadline, (root/'stderr').read_text()
                    time.sleep(.01)
                preserved = fixture.saves
                # Ensure there is unsaved state, so the final-write assertions
                # and deliberate save failure actually exercise disk I/O.
                minimum_frames = len(fixture.frames)+5
                while len(fixture.frames)<minimum_frames:
                    assert process.poll() is None
                    assert time.monotonic()<deadline
                    time.sleep(.005)
                children = Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
                assert len(children)==1, children
                if scenario=='reset-held-menu':
                    fixture.put('STATUS',1)
                    held_at=len(fixture.frames)
                    while len(fixture.frames)<held_at+8:
                        assert process.poll() is None
                        assert time.monotonic()<deadline
                        time.sleep(.005)
                elif scenario=='term-audio-stalled':
                    fixture.hold_audio = True
                    pending_until = time.monotonic()+.25
                    while ((fixture.get('AUDIO_WRITE')-fixture.get('AUDIO_READ')) & 0xffffffff)<=800:
                        assert time.monotonic()<pending_until
                        time.sleep(.001)
                    time.sleep(.003)
                elif scenario=='menu-pending':
                    # Continue the DAC while withholding video ACKs, so the
                    # runtime can actually reach a blocked video publication.
                    fixture.hold_video = True
                elif scenario not in ('term-active','transient-name'):
                    # Hold acknowledgements and retain the valid stale identity.
                    fixture.stop.set()
                    fixture.thread.join()
                started = time.monotonic()
                if scenario in ('term-active','term-audio-stalled'):
                    process.send_signal(signal.SIGTERM)
                elif scenario=='transient-name':
                    selected.write_text('')
                    rewrite=threading.Timer(.02,lambda:selected.write_text('TIC-80'))
                    rewrite.start()
                    resumed_at=len(fixture.frames)+8
                    while len(fixture.frames)<resumed_at:
                        assert process.poll() is None, (root/'stderr').read_text()
                        assert time.monotonic()-started<.5
                        time.sleep(.005)
                    rewrite.join()
                    assert Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()==children
                    process.send_signal(signal.SIGTERM)
                elif scenario in ('menu','menu-pending','other-core','reset-held-menu'):
                    if scenario=='menu-pending':
                        pending_until=time.monotonic()+.25
                        while fixture.get('VIDEO_PUBLISH')==fixture.get('VIDEO_PRESENTED'):
                            assert time.monotonic()<pending_until
                            time.sleep(.001)
                        time.sleep(.04)  # next publication is blocked on that ACK
                        fixture.stop.set()
                        fixture.thread.join()
                    select(selected, 'AO486' if scenario=='other-core' else 'MENU')
                elif scenario=='missing-name':
                    selected.unlink()
                elif scenario=='lost-identity':
                    fixture.put('IDENTITY',0)
                elif scenario=='stalled-tic80':
                    pass  # name/identity remain TIC-80; stopped ACKs are a fault.
                elif scenario=='save-failure-on-menu':
                    preserved = root/'preserved-saves'
                    fixture.saves.rename(preserved)
                    fixture.saves.write_text('not a directory')
                    select(selected,'MENU')
                else: raise AssertionError(scenario)
                wait_states = []
                while process.poll() is None:
                    waiting = time.monotonic()-started
                    if waiting>.2 and not wait_states:
                        for task in Path(f'/proc/{process.pid}/task').glob('*'):
                            try:
                                wait_states.append((task.name,(task/'wchan').read_text(),(task/'syscall').read_text()))
                            except FileNotFoundError:
                                pass
                    # Reconfiguration on an explicitly selected TIC-80 has a
                    # ten-second recovery budget. Unknown selection still
                    # fails promptly; intentional departures keep their tighter
                    # sub-second check below.
                    budget = 12 if scenario in ('lost-identity','stalled-tic80') else 4
                    assert waiting<budget, (scenario,wait_states)
                    time.sleep(.005)
                elapsed = time.monotonic()-started
                output, errors = (root/'stdout').read_text(), (root/'stderr').read_text()
                good = scenario in ('term-active','term-audio-stalled','transient-name','menu','menu-pending','other-core','reset-held-menu')
                assert process.returncode==(0 if good else 1), (scenario,output,errors)
                assert f'error={0 if good else 1}' in output, (scenario,output,errors)
                if good:
                    assert elapsed<.75, (scenario,elapsed,output,errors,wait_states)
                    assert 'timed out' not in errors and 'stalled' not in errors, errors
                if scenario in ('menu','menu-pending','other-core','reset-held-menu'):
                    assert 'Core switched; runtime stopped' in output
                if scenario=='transient-name':
                    assert 'Cartridge restarted:' not in output, output
                if scenario=='stalled-tic80':
                    assert 'timed out' in errors or 'stalled' in errors, errors
                if scenario=='save-failure-on-menu':
                    assert 'Not a directory' in errors, errors
                final = save_values(preserved,key)
                assert final[0]>=before[0] and final[1]==1, (scenario,final[:2],before[:2])
                if good:
                    assert final[0]>before[0], (scenario,'final state was not flushed',final[:2])
                assert not list(preserved.glob('*.tmp-*'))
                assert not Path(f'/proc/{process.pid}').exists()
                assert all(not Path('/proc/'+child).exists() for child in children), children
                print(f'{scenario}: exit={process.returncode}, {elapsed:.3f}s, CRC-valid save, BOOT=1')
        finally:
            fixture.close()


if __name__=='__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--service',required=True)
    a = p.parse_args()
    # The fixture flag cannot redirect the selection check on physical DDR.
    override = subprocess.run([a.service,'--serve','/unused','--core-name','/unused'],capture_output=True)
    assert override.returncode==2
    for scenario in ('term-active','term-audio-stalled','transient-name','menu','menu-pending','other-core','reset-held-menu','missing-name','lost-identity',
                     'stalled-tic80','save-failure-on-menu'):
        run(a.service,scenario)
