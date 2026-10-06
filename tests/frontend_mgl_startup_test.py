"""Real frontend/worker IPC: no cached BOOT during stock Main's MGL delay."""
import argparse
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from service_recovery_test import Fixture, ordinary, save_values


def run(frontend, studio, scenario, omit_context=False):
    with tempfile.TemporaryDirectory(prefix='tic80-mgl-startup-') as directory:
        root = Path(directory)
        fixture = Fixture(root)
        core = root/'CORENAME'; core.write_text('TIC-80')
        proc = root/'proc'; process_dir = proc/'123'; process_dir.mkdir(parents=True)
        (process_dir/'exe').symlink_to('/media/fat/MiSTer')
        mgl = root/'delayed.mgl'
        first = '<reset delay="0" hold="0"/>' if scenario == 'first-reset' else ''
        mgl.write_text('<mistergamedescription>'+first+
                      '<file delay="300" type="F" index="0" path="new.tic"/>'+
                      '</mistergamedescription>')
        raw_argv = b'/media/fat/MiSTer\0/media/fat/TIC80.rbf\0'
        cmdline = process_dir/'cmdline'; cmdline.write_bytes(raw_argv)
        initial = root/'initial.tic'; initial.write_bytes(ordinary('mgl-old',6))
        folder = root/'studio'; folder.mkdir()
        if studio:
            argv = [frontend,'--folder',str(folder),'--saves',str(fixture.saves),
                    '--cart',str(initial),'--run']
        else:
            argv = [frontend,'--serve',str(fixture.saves)]
            fixture.actions = [(5,lambda:fixture.transfer(6,initial.read_bytes()))]
        argv += ['--memory',str(fixture.memory),'--core-name',str(core)]
        if not omit_context: argv += ['--main-processes',str(proc)]
        try:
            with (root/'output').open('w+b') as out:
                fixture.thread.start()
                process = fixture.process = subprocess.Popen(argv,stdout=out,stderr=subprocess.STDOUT)
                def output(): return (root/'output').read_text()
                def values(key):
                    try: return save_values(fixture.saves,key)
                    except FileNotFoundError: return [0]*256
                def wait(predicate,seconds=6,reason='Frontend did not reach the expected state'):
                    deadline=time.monotonic()+seconds
                    while not predicate():
                        assert process.poll() is None,output()
                        assert time.monotonic()<deadline,(reason,output())
                        time.sleep(.005)
                wait(lambda:values('mgl-old')[0]>=60)
                assert values('mgl-old')[1]==1
                if scenario!='raw': cmdline.write_bytes(raw_argv+str(mgl).encode()+b'\0')
                if scenario in ('transient-raw','transient-mgl'):
                    # A process observation during exec need not return a
                    # complete argv vector. It cannot authorize cached BOOT.
                    cmdline.write_bytes(raw_argv[:-1])
                if scenario=='missing': mgl.unlink()
                # Fresh FPGA session exists before Main finishes initialization.
                fixture.put('SESSION_ACK',0)
                at=len(fixture.frames); wait(lambda:len(fixture.frames)>=at+8)
                core.write_text('TIC-80')
                if scenario in ('raw','first-reset'):
                    wait(lambda:values('mgl-old')[1]>=2)
                    assert values('mgl-old')[1]==2,output()
                else:
                    if not omit_context: wait(lambda:'waiting for initial mgl cartridge' in output().lower())
                    # Allow every in-flight checkpoint/save to finish before
                    # measuring the hold, then delay longer than 60 game ticks.
                    time.sleep(.3)
                    before=values('mgl-old')[:2]
                    time.sleep(1.2)
                    after=values('mgl-old')[:2]
                    assert after[1]==1,('Cached BOOT before delayed MGL transfer',before,after,output())
                    assert after==before,('Cached TIC during delayed MGL transfer',before,after,output())
                    if scenario in ('transient-raw','transient-mgl'):
                        cmdline.write_bytes(raw_argv+(str(mgl).encode()+b'\0' if scenario=='transient-mgl' else b''))
                        wait(lambda:'launch context resolved:' in output().lower(),
                             reason='Main launch context did not resolve after coherent argv became available')
                        if scenario=='transient-raw':
                            wait(lambda:values('mgl-old')[1]>=2)
                            assert values('mgl-old')[1]==2,output()
                        else:
                            time.sleep(.3)
                            assert values('mgl-old')[:2]==after,output()
                            ticket=6 if studio else 10
                            fixture.transfer(ticket,ordinary('mgl-new',12))
                            wait(lambda:values('mgl-new')[0]>=60)
                            assert values('mgl-new')[1]==1 and values('mgl-old')[1]==1,output()
                    if scenario in ('replacement','missing'):
                        next_ticket=6 if studio else 10
                        fixture.transfer(next_ticket,ordinary('mgl-new',12))
                        wait(lambda:values('mgl-new')[0]>=60)
                        assert values('mgl-new')[1]==1 and values('mgl-old')[1]==1,output()
                    elif scenario=='rejected':
                        ticket=6 if studio else 10
                        fixture.transfer(ticket,b'')
                        wait(lambda:fixture.get('CART_ACK')==ticket)
                        wait(lambda:values('mgl-old')[0]>after[0]+60,seconds=7)
                        assert values('mgl-old')[1]==(2 if studio else 1),output()
                    elif scenario=='cancel-reset':
                        fixture.put('STATUS',1)
                        at=len(fixture.frames); wait(lambda:len(fixture.frames)>=at+8)
                        fixture.put('STATUS',0)
                        wait(lambda:values('mgl-old')[1]>=2)
                        assert values('mgl-old')[1]==2,output()
                    elif scenario=='departure':
                        core.write_text('MENU'); process.wait(timeout=3)
                        assert process.returncode==0,output()
                if process.poll() is None:
                    process.send_signal(signal.SIGTERM); process.wait(timeout=4)
                assert process.returncode==0,output()
                assert not list(fixture.saves.glob('*.tmp-*'))
                assert values('mgl-old')[1]==(2 if scenario in ('raw','first-reset','cancel-reset','transient-raw') or
                                             (studio and scenario=='rejected') else 1),output()
                if studio:
                    import re
                    for pid in set(map(int,re.findall(r'worker=(\d+)',output()))):
                        assert not Path('/proc/'+str(pid)).exists(),pid
                print(('Studio' if studio else 'player')+' '+scenario+
                      ': exact BOOT counters, delayed hold, saves and clean termination passed',flush=True)
        finally: fixture.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend',required=True)
    parser.add_argument('--studio',action='store_true')
    parser.add_argument('--omit-context-negative-control',action='store_true')
    parser.add_argument('--scenario',choices=('raw','first-reset','replacement','missing','rejected','transient-raw','transient-mgl','cancel-reset','departure','stop'),required=True)
    args=parser.parse_args()
    run(args.frontend,args.studio,args.scenario,args.omit_context_negative_control)
