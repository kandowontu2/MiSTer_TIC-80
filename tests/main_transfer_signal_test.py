"""Exercise the actual pidfd instrument against owned children and fixture DDR."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

parser=argparse.ArgumentParser()
parser.add_argument('instrument',type=Path)
args,remaining=parser.parse_known_args()
INSTRUMENT=args.instrument.resolve()
MAGIC=0x33434954


class TransferSignal(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder=Path(self.temporary.name)
        self.core=self.folder/'CORENAME';self.core.write_text('TIC-80\n')
        self.memory=self.folder/'DDR';self.memory.write_bytes(bytes(4096))
        self.patch(0,MAGIC);self.patch(8,(144<<16)|256)
        self.patch(0x20,1);self.patch(0x28,1);self.patch(0x30,100)
        self.transfer(6,21)
        self.target=subprocess.Popen(['/usr/bin/sleep','30'],stdin=subprocess.DEVNULL,
                                     stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        self.addCleanup(self.stop_target)

    def stop_target(self):
        if self.target.poll() is None:self.target.kill()
        self.target.wait(timeout=5)

    def patch(self,offset,value):
        with self.memory.open('r+b',buffering=0) as f:os.pwrite(f.fileno(),struct.pack('<I',value),offset)

    def transfer(self,ticket,size):
        with self.memory.open('r+b',buffering=0) as f:os.pwrite(f.fileno(),struct.pack('<II',ticket,size),0x70)

    def command(self,signo='KILL',seconds='1'):
        exe=os.readlink(f'/proc/{self.target.pid}/exe')
        fields=Path(f'/proc/{self.target.pid}/stat').read_text().rsplit(')',1)[1].split()
        digest=hashlib.sha256(Path(exe).read_bytes()).hexdigest()
        return [str(INSTRUMENT),'--pid',str(self.target.pid),'--starttime',fields[19],
                '--exe',exe,'--sha256',digest,'--signal',signo,'--seconds',seconds,
                '--core-name',str(self.core),'--memory',str(self.memory)]

    def start(self,command=None):
        process=subprocess.Popen(command or self.command(),text=True,stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.addCleanup(self.stop_instrument,process)
        ready,_,_=select.select([process.stdout],[],[],3)
        self.assertTrue(ready,'Instrument did not arm or report a failure')
        line=process.stdout.readline()
        if not line:self.fail(process.stderr.read())
        self.assertEqual(json.loads(line)['event'],'armed')
        return process,line

    def stop_instrument(self,process):
        if process.poll() is None:process.terminate()
        process.communicate(timeout=6)

    def finish(self,process,first):
        output,error=process.communicate(timeout=7)
        return process.returncode,[json.loads(line) for line in (first+output).splitlines()],error

    def test_each_signal_targets_one_owned_process_and_preserves_DDR(self):
        for name,signo in [('KILL',signal.SIGKILL),('TERM',signal.SIGTERM)]:
            with self.subTest(signal=name):
                if self.target.poll() is not None:
                    self.target=subprocess.Popen(['/usr/bin/sleep','30'])
                self.transfer(6,21)
                process,first=self.start(self.command(name))
                self.transfer(9,2048);expected=self.memory.read_bytes()
                status,rows,error=self.finish(process,first)
                self.assertEqual(status,0,error)
                self.assertEqual([row['event'] for row in rows],['armed','dispatch','departed'])
                self.assertEqual(rows[1]['signal'],signo)
                self.assertTrue(rows[1]['single_dispatch'])
                self.assertTrue(rows[2]['still_receiving'])
                self.assertTrue(rows[2]['within_window'])
                self.assertEqual(rows[1]['signal_result'],0)
                self.assertGreaterEqual(rows[1]['signal_return_ns'],rows[1]['dispatch_ns'])
                self.assertEqual(rows[1]['ticket'],rows[2]['ticket'])
                self.assertGreaterEqual(rows[1]['dispatch_ns'],rows[1]['sampled_ns'])
                self.assertEqual(self.target.wait(timeout=3),-signo)
                self.assertEqual(self.memory.read_bytes(),expected)

    def test_wrong_identity_and_source_guards_never_signal(self):
        cases=[('--starttime','1'),('--sha256','0'*64),('--exe','/unrelated/program')]
        for option,value in cases:
            with self.subTest(option=option):
                command=self.command();command[command.index(option)+1]=value
                result=subprocess.run(command,text=True,capture_output=True,timeout=3)
                self.assertEqual(result.returncode,1,result.stderr)
                self.assertNotIn('dispatch',result.stdout)
                self.assertIsNone(self.target.poll())
        for offset,value in [(0,0),(8,0),(0x20,0),(0x28,0)]:
            with self.subTest(offset=offset):
                original=self.memory.read_bytes();self.patch(offset,value)
                result=subprocess.run(self.command(),text=True,capture_output=True,timeout=3)
                self.assertEqual(result.returncode,1,result.stderr)
                self.assertNotIn('dispatch',result.stdout)
                self.assertIsNone(self.target.poll());self.memory.write_bytes(original)
        for name in ('MENU\n','TIC-80\nOTHER\n','TIC-80\0\n'):
            self.core.write_bytes(name.encode())
            result=subprocess.run(self.command(),text=True,capture_output=True,timeout=3)
            self.assertEqual(result.returncode,1,result.stderr)
            self.assertNotIn('dispatch',result.stdout);self.assertIsNone(self.target.poll())

    def test_pending_transfer_at_start_is_not_mistaken_for_new_work(self):
        self.transfer(9,2048)
        result=subprocess.run(self.command(),text=True,capture_output=True,timeout=3)
        self.assertEqual(result.returncode,1,result.stderr)
        self.assertFalse(result.stdout);self.assertIsNone(self.target.poll())

    def test_core_departure_after_arming_prevents_signal(self):
        process,first=self.start();self.core.write_text('MENU\n');self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error)
        self.assertEqual([r['event'] for r in rows],['armed']);self.assertIsNone(self.target.poll())

    def test_target_departure_after_arming_prevents_signal(self):
        process,first=self.start();self.target.terminate();self.target.wait(timeout=3)
        self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error);self.assertEqual([r['event'] for r in rows],['armed'])

    def test_only_receiving_within_bounds_triggers(self):
        for ticket,size in [(10,2048),(11,2048),(9,1023),(9,65537)]:
            with self.subTest(ticket=ticket,size=size):
                self.transfer(6,21);process,first=self.start();self.transfer(ticket,size)
                status,rows,error=self.finish(process,first)
                self.assertEqual(status,1,error)
                self.assertEqual([r['event'] for r in rows],['armed']);self.assertIsNone(self.target.poll())

    def test_session_mismatch_prevents_signal(self):
        process,first=self.start();self.patch(0x20,2);self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error)
        self.assertEqual([r['event'] for r in rows],['armed']);self.assertIsNone(self.target.poll())

    def test_stopped_heartbeat_prevents_late_trigger(self):
        process,first=self.start(self.command(seconds='2'))
        time.sleep(1.15);self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error)
        self.assertEqual([r['event'] for r in rows],['armed']);self.assertIsNone(self.target.poll())

    def test_ignored_signal_is_not_retried(self):
        self.stop_target()
        ready=self.folder/'ignore-ready'
        self.target=subprocess.Popen([sys.executable,'-c',
            'import signal,time,pathlib,sys; signal.signal(signal.SIGTERM,signal.SIG_IGN); '
            'pathlib.Path(sys.argv[1]).write_text("ready"); time.sleep(30)',str(ready)])
        deadline=time.monotonic()+3
        while not ready.exists():
            self.assertLess(time.monotonic(),deadline);time.sleep(.01)
        process,first=self.start(self.command('TERM'))
        self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error)
        self.assertEqual([r['event'] for r in rows],['armed','dispatch'])
        self.assertTrue(rows[1]['single_dispatch']);self.assertIsNone(self.target.poll())

    def test_executable_replacement_after_arming_prevents_signal(self):
        self.stop_target()
        ready=self.folder/'exec-ready';change=self.folder/'exec-change'
        self.target=subprocess.Popen([sys.executable,'-c',
            'import os,pathlib,sys,time; pathlib.Path(sys.argv[1]).write_text("ready"); '
            '\nwhile not pathlib.Path(sys.argv[2]).exists(): time.sleep(.005)'
            '\nos.execv("/usr/bin/sleep",["sleep","30"])',str(ready),str(change)])
        deadline=time.monotonic()+3
        while not ready.exists():
            self.assertLess(time.monotonic(),deadline);time.sleep(.005)
        original=os.readlink(f'/proc/{self.target.pid}/exe')
        process,first=self.start(self.command(seconds='2'));change.write_text('exec')
        while os.readlink(f'/proc/{self.target.pid}/exe')==original:
            self.assertLess(time.monotonic(),deadline);time.sleep(.005)
        self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error)
        self.assertEqual([r['event'] for r in rows],['armed']);self.assertIsNone(self.target.poll())

    def test_cancellation_after_arming_does_not_signal_target(self):
        process,first=self.start();process.terminate()
        status,rows,error=self.finish(process,first)
        self.transfer(9,2048)
        self.assertEqual(status,1,error)
        self.assertEqual([r['event'] for r in rows],['armed']);self.assertIsNone(self.target.poll())

    def test_completed_transfer_at_departure_is_not_qualified(self):
        self.stop_target()
        ready=self.folder/'term-ready'
        self.target=subprocess.Popen([sys.executable,'-c',
            'import signal,sys,pathlib,struct,os,time; '
            '\ndef finish(signo,frame):'
            '\n with open(sys.argv[2],"r+b",buffering=0) as f: os.pwrite(f.fileno(),struct.pack("<II",10,2048),0x70)'
            '\n sys.exit(0)'
            '\nsignal.signal(signal.SIGTERM,finish); pathlib.Path(sys.argv[1]).write_text("ready"); time.sleep(30)',
            str(ready),str(self.memory)])
        deadline=time.monotonic()+3
        while not ready.exists():
            self.assertLess(time.monotonic(),deadline);time.sleep(.005)
        process,first=self.start(self.command('TERM'));self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error)
        self.assertEqual([r['event'] for r in rows],['armed','dispatch','departed'])
        self.assertFalse(rows[2]['still_receiving']);self.assertEqual(rows[2]['ticket'],10)
        self.assertEqual(self.target.wait(timeout=3),0)

    def test_full_log_pipe_cannot_delay_signal_dispatch(self):
        process,first=self.start()
        # Backpressure the actual stdout fd after its armed record. The target
        # must receive its single signal before the probe can write another log.
        writer=os.open(f'/proc/{process.pid}/fd/1',os.O_WRONLY|os.O_NONBLOCK)
        with os.fdopen(writer,'wb',buffering=0):
            fcntl.fcntl(writer,fcntl.F_SETPIPE_SZ,4096)
            filled=0
            while True:
                try:filled+=os.write(writer,b'.'*512)
                except BlockingIOError:break
            self.assertGreaterEqual(filled,4096)
            self.transfer(9,2048)
            self.assertEqual(self.target.wait(timeout=1),-signal.SIGKILL)
            remaining=filled
            while remaining:
                raw=os.read(process.stdout.fileno(),remaining)
                self.assertTrue(raw);self.assertEqual(raw,b'.'*len(raw));remaining-=len(raw)
        # The extra writer must close before communicate waits for stdout EOF.
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,0,error)
        self.assertEqual([r['event'] for r in rows],['armed','dispatch','departed'])
        self.assertTrue(rows[1]['single_dispatch']);self.assertEqual(rows[1]['signal_result'],0)

    def test_incomplete_transfer_outside_byte_window_is_still_rejected(self):
        self.stop_target()
        ready=self.folder/'outside-window-ready'
        self.target=subprocess.Popen([sys.executable,'-c',
            'import signal,sys,pathlib,struct,os,time; '
            '\ndef finish(signo,frame):'
            '\n with open(sys.argv[2],"r+b",buffering=0) as f: os.pwrite(f.fileno(),struct.pack("<II",9,70000),0x70)'
            '\n sys.exit(0)'
            '\nsignal.signal(signal.SIGTERM,finish); pathlib.Path(sys.argv[1]).write_text("ready"); time.sleep(30)',
            str(ready),str(self.memory)])
        deadline=time.monotonic()+3
        while not ready.exists():
            self.assertLess(time.monotonic(),deadline);time.sleep(.005)
        process,first=self.start(self.command('TERM'));self.transfer(9,2048)
        status,rows,error=self.finish(process,first)
        self.assertEqual(status,1,error)
        self.assertTrue(rows[2]['still_receiving']);self.assertFalse(rows[2]['within_window'])
        self.assertEqual(rows[2]['ticket'],9);self.assertEqual(rows[2]['bytes'],70000)
        self.assertEqual(self.target.wait(timeout=3),0)

    def test_invalid_options_and_short_memory_are_rejected(self):
        for option,value in [('--pid','1'),('--signal','STOP'),('--seconds','61'),('--min-bytes','0')]:
            with self.subTest(option=option):
                command=self.command();
                if option in command:command[command.index(option)+1]=value
                else:command.extend([option,value])
                result=subprocess.run(command,text=True,capture_output=True,timeout=3)
                self.assertEqual(result.returncode,2,result.stderr)
                self.assertFalse(result.stdout);self.assertIsNone(self.target.poll())
        self.memory.write_bytes(bytes(4095))
        result=subprocess.run(self.command(),text=True,capture_output=True,timeout=3)
        self.assertEqual(result.returncode,1,result.stderr)
        self.assertFalse(result.stdout);self.assertIsNone(self.target.poll())


if __name__=='__main__':unittest.main(argv=[sys.argv[0],*remaining])
