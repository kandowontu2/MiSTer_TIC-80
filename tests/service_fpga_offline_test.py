"""FPGA configuration can outlast one handshake and capture a stale nonce."""
import argparse
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from service_recovery_test import Fixture, cartridge, save_values, PROTOCOL


class Configuration(Fixture):
    def __init__(self, root, selected, mode):
        self.resume = self.baseline = None
        self.armed = False
        self.selected, self.mode = selected, mode
        super().__init__(root)

    def get(self, name):
        value = super().get(name)
        if name == 'SESSION_REQUEST' and self.resume is not None:
            if self.mode == 'timeout' or time.monotonic() < self.resume:
                return 0
            if self.baseline is None:
                # The newly configured FPGA cannot accept the ARM word it
                # first finds in stale DDR. Only a later nonce establishes it.
                self.baseline = value
                self.put('IDENTITY',PROTOCOL['magic'])
                if self.mode == 'replacement':
                    self.transfer(10,cartridge('replacement','function BOOT() pmem(1,pmem(1)+1) end\n'
                        'function TIC() pmem(0,pmem(0)+1); cls(6) end\n'))
                self.selected.write_text('TIC-80')
            # tic80_ddr_video keeps session_armed set after any later word,
            # including the zero request used to quiesce a failed handshake.
            if value != self.baseline:
                self.armed = True
            if not self.armed:
                return 0
        return value


def run(service, mode):
    with tempfile.TemporaryDirectory(prefix='tic80-configure-') as directory:
        root = Path(directory); selected = root/'CORENAME'; selected.write_text('TIC-80')
        fixture = Configuration(root,selected,mode)
        key = 'fpga-configuration'
        payload = cartridge(key,'function BOOT() pmem(1,pmem(1)+1) end\n'
                            'function TIC() pmem(0,pmem(0)+1); cls(8) end\n')
        fixture.actions = [(5,lambda:fixture.transfer(6,payload))]
        try:
            with (root/'stdout').open('w+b') as out,(root/'stderr').open('w+b') as err:
                fixture.thread.start()
                process = fixture.process = subprocess.Popen([service,'--serve',str(fixture.saves),
                    '--memory',str(fixture.memory),'--core-name',str(selected)],stdout=out,stderr=err)
                def values():
                    try:return save_values(fixture.saves,key)
                    except FileNotFoundError:return [0]*256
                def wait(predicate,seconds):
                    deadline=time.monotonic()+seconds
                    while not predicate():
                        assert process.poll() is None,(root/'stdout').read_text()+(root/'stderr').read_text()
                        assert time.monotonic()<deadline
                        time.sleep(.005)
                wait(lambda:values()[0]>=60,4)
                before=values()[0];started=time.monotonic();fixture.resume=started+2.2
                if mode=='identity': fixture.put('IDENTITY',0)
                if mode in ('departure','stop'):
                    time.sleep(.25)
                    if mode=='departure':selected.write_text('MENU')
                    else:process.send_signal(signal.SIGTERM)
                    process.wait(timeout=2)
                    assert process.returncode==0 and values()[1]==1
                    if mode=='departure':assert 'Core switched; runtime stopped' in (root/'stdout').read_text()
                elif mode=='timeout':
                    process.wait(timeout=13)
                    assert process.returncode==1 and 9.5<=time.monotonic()-started<=12
                    assert values()[1]==1
                else:
                    if mode=='replacement':
                        def replaced():
                            try:return save_values(fixture.saves,'replacement')[0]>=60
                            except FileNotFoundError:return False
                        wait(replaced,8)
                    else:wait(lambda:values()[1]==2 and values()[0]>=before+60,8)
                    process.send_signal(signal.SIGTERM);process.wait(timeout=3)
                    assert process.returncode==0
                    if mode=='replacement':
                        assert values()[1]==1 and save_values(fixture.saves,'replacement')[1]==1
                        assert 'Cartridge restarted:' not in (root/'stdout').read_text()
                    else:
                        assert values()[1]==2
                        assert (root/'stdout').read_text().count('Cartridge restarted:')==1
                print(mode+': configuration pause/stale nonce, bounded recovery or clean departure verified',flush=True)
        finally:fixture.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--service',required=True)
    parser.add_argument('--mode',choices=('offline','identity','replacement','departure','stop','timeout'),default='offline')
    args=parser.parse_args();run(args.service,args.mode)
