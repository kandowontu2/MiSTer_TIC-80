"""A bad output acknowledgment in a live FPGA session is not a Main reload.

Inject a foreign video ACK or impossible audio counter, keeping Main's file
generation and the actual session ACK unchanged. Transport may flush itself,
but the running cartridge must continue without another BOOT or a reset wait.
"""
import argparse
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from service_recovery_test import Fixture, cartridge, save_values


class OutputFault(Fixture):
    def __init__(self, root):
        self.bad_session = None
        self.recovered = False
        super().__init__(root)

    def get(self, name):
        value = super().get(name)
        if name == 'SESSION_REQUEST' and self.bad_session is not None and value != self.bad_session:
            self.hold_audio = self.hold_video = False
            self.recovered = True
        return value

    def inject(self, fault):
        self.bad_session = self.get('SESSION_REQUEST')
        assert self.bad_session and self.get('SESSION_ACK') == self.bad_session
        if fault == 'frame':
            self.hold_video = True
            self.put('VIDEO_PRESENTED',0xfffffffe)
        else:
            self.hold_audio = True
            self.put('AUDIO_READ',self.get('AUDIO_WRITE')+2)


def run(service, fault, frontend='player'):
    with tempfile.TemporaryDirectory(prefix='tic80-live-output-') as directory:
        root = Path(directory);fixture = OutputFault(root)
        selected = root/'CORENAME';selected.write_text('TIC-80')
        generation = selected.stat().st_mtime_ns
        key = 'live-output-' + fault
        payload = cartridge(key,'function BOOT() pmem(1,pmem(1)+1) end\n'
                            'function TIC() pmem(0,pmem(0)+1); cls(6) end\n')
        fixture.actions = [(5,lambda: fixture.transfer(6,payload))]
        try:
            with (root/'stdout').open('w+b') as out, (root/'stderr').open('w+b') as err:
                fixture.thread.start()
                if frontend=='studio':
                    folder=root/'studio';folder.mkdir()
                    command=[service,'--folder',str(folder),'--saves',str(fixture.saves)]
                else:command=[service,'--serve',str(fixture.saves)]
                process = fixture.process = subprocess.Popen(command+['--memory',str(fixture.memory),
                    '--core-name',str(selected)],stdout=out,stderr=err)
                def wait(predicate, seconds=4):
                    deadline=time.monotonic()+seconds
                    while not predicate():
                        assert process.poll() is None,(root/'stderr').read_text()
                        assert time.monotonic()<deadline,(root/'stdout').read_text()+(root/'stderr').read_text()
                        time.sleep(.005)
                def values():
                    try:return save_values(fixture.saves,key)
                    except FileNotFoundError:return [0]*256
                wait(lambda:values()[0]>=60)
                before=values()[0]
                children=Path(f'/proc/{process.pid}/task/{process.pid}/children')
                worker=children.read_text().split();assert len(worker)==1,worker
                fixture.inject(fault)
                wait(lambda:fixture.recovered)
                wait(lambda:values()[0]>=before+60)
                assert values()[1]==1, 'Output recovery repeated BOOT'
                assert children.read_text().split()==worker, 'Output recovery replaced the interpreter'
                assert selected.stat().st_mtime_ns==generation
                process.send_signal(signal.SIGTERM);process.wait(timeout=3)
                assert process.returncode==0,(root/'stderr').read_text()
                assert 'Cartridge restarted:' not in (root/'stdout').read_text()
                assert save_values(fixture.saves,key)[1]==1
                print(f'{frontend}/{fault}: transport recovered; interpreter retained, BOOT=1, no Main rewrite',flush=True)
        finally:fixture.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--service',required=True)
    parser.add_argument('--fault',choices=('frame','audio'))
    parser.add_argument('--frontend',choices=('player','studio'),default='player')
    args=parser.parse_args()
    for fault in ([args.fault] if args.fault else ['frame','audio']):run(args.service,fault,args.frontend)
