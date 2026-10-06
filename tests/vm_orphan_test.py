"""Reap a real worker after killing its supervisor, using native Linux prctl.

The existing ARM --orphan-probe runs unchanged under QEMU. QEMU 8.2 does not
implement PR_SET_CHILD_SUBREAPER, so this native Python process owns adoption
and waitpid; the guest worker still sets and receives its real PDEATHSIG.
"""
import argparse,ctypes,os,select,signal,struct,subprocess,time
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    args=parser.parse_args(); fixture=args.fixture.resolve()
    with fixture.open('rb') as stream: header=stream.read(6)
    assert header[:4]==b'\x7fELF' and header[5]==1, 'Fixture must be little-endian ELF'
    libc=ctypes.CDLL(None,use_errno=True)
    assert libc.prctl(36,1,0,0,0)==0, f'native subreaper: errno={ctypes.get_errno()}'
    readfd,writefd=os.pipe(); worker=None; probe=None
    # Run in a single-threaded process. Preserve fd 3 through close_fds, then
    # duplicate the writer there for the existing probe's pid publication.
    def publish_fd(): os.dup2(writefd,3)
    try:
        probe=subprocess.Popen([str(fixture),'--orphan-probe'],pass_fds=tuple({3,writefd}),preexec_fn=publish_fd)
        os.close(writefd); writefd=-1
        assert select.select([readfd],[],[],10)[0], 'Probe did not publish worker pid'
        packet=os.read(readfd,4); assert len(packet)==4
        worker=struct.unpack('<i',packet)[0]; assert worker>0 and worker!=probe.pid
        time.sleep(.05)
        os.kill(probe.pid,signal.SIGKILL); assert probe.wait(timeout=2)==-signal.SIGKILL
        started=time.monotonic()
        while True:
            waited,status=os.waitpid(worker,os.WNOHANG)
            if waited: break
            assert time.monotonic()-started<2, 'Worker survived supervisor death'
            time.sleep(.001)
        assert waited==worker
        assert (os.WIFSIGNALED(status) and os.WTERMSIG(status)==signal.SIGKILL) or (os.WIFEXITED(status) and os.WEXITSTATUS(status)==0)
        worker=None
        try: os.waitpid(-1,os.WNOHANG)
        except ChildProcessError: pass
        else: raise AssertionError('A descendant remains after reaping the worker')
        print('External native supervisor: guest worker dies after supervisor SIGKILL; adopted and reaped; no descendants remain',flush=True)
    finally:
        os.close(readfd)
        if writefd>=0: os.close(writefd)
        if probe is not None and probe.poll() is None:
            probe.kill(); probe.wait(timeout=2)
        if worker is not None:
            try: os.kill(worker,signal.SIGKILL)
            except ProcessLookupError: pass
            try: os.waitpid(worker,0)
            except ChildProcessError: pass

if __name__=='__main__': main()
