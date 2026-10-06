"""Pace the actual Studio frontend while a save-identity flush is delayed."""
import argparse
import ctypes
import json
import mmap
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from audio_fixture import AudioClock

parser=argparse.ArgumentParser()
parser.add_argument('--frontend',required=True)
parser.add_argument('--fault-library',required=True)
parser.add_argument('--failure',action='store_true')
args=parser.parse_args()
protocol=json.loads((Path(__file__).resolve().parents[1]/'protocol/memory_map.json').read_text())
offsets=protocol['offsets']
with tempfile.TemporaryDirectory(prefix='tic80-studio-slow-transport-') as directory:
    root=Path(directory); folder=root/'studio'; folder.mkdir(); saves=root/'saves'; saves.mkdir()
    core=root/'CORENAME'; core.write_text('TIC-80\n')
    failure=root/'fail-writes'
    if args.failure: failure.touch()
    # No explicit saveid: the first TIC changes bank0, so Ctrl+R changes the
    # save identity while the original identity's background fsync is delayed.
    code=b'function TIC() pmem(0,pmem(0)+1) cls(6) mset(0,0,42) sync(4,0,true) end\n'
    cart=root/'cart.tic'; cart.write_bytes(bytes([17,0,0,0,5,len(code),0,0])+code)
    preload=root/'slow.so'; shutil.copyfile(args.fault_library,preload)
    memory=root/'ddr'
    with memory.open('w+b') as file:
        file.truncate(protocol['region_bytes'])
        with mmap.mmap(file.fileno(),protocol['region_bytes']) as shared:
            def get(name): return struct.unpack_from('<I',shared,offsets[name])[0]
            def put(name,value): ctypes.c_uint32.from_buffer(shared,offsets[name]).value=value&0xffffffff
            def keys(held):
                words=[0]*16
                for key in held: words[key//32]|=1<<(key%32)
                sequence=get('KEYBOARD'); put('KEYBOARD',sequence+1)
                struct.pack_into('<16I',shared,offsets['KEYBOARD_BITS'],*words)
                put('KEYBOARD',sequence+2)
            put('IDENTITY',protocol['magic']); put('GEOMETRY',protocol['width']|protocol['height']<<16)
            put('KEYBOARD',2)
            stop=threading.Event(); failures=[]; frames=[]; frozen=[]; clock=AudioClock()
            def emulate():
                try:
                    session=presented=0
                    while not stop.is_set():
                        put('HEARTBEAT',get('HEARTBEAT')+1)
                        request=get('SESSION_REQUEST')
                        if request!=session:
                            session=request; presented=0; clock.reset()
                            put('VIDEO_PRESENTED',0); put('AUDIO_READ',0); put('SESSION_ACK',session)
                        put('AUDIO_READ',clock.sample(get('AUDIO_WRITE')))
                        publication=get('VIDEO_PUBLISH')
                        if session and publication&2 and publication!=presented:
                            offset=offsets['BUFFER1'] if publication&1 else offsets['BUFFER0']
                            picture=bytes(shared[offset:offset+protocol['frame_bytes']])
                            assert all(alpha==255 for alpha in picture[3::4])
                            frames.append(zlib.crc32(picture)); put('VIDEO_PRESENTED',publication); presented=publication
                            if len(frames)==10: keys([0x14,0x2d]) # physical Ctrl+R
                            if len(frames)==15: keys([])
                            if args.failure and len(frames)==65:
                                assert not list(saves.glob('*.pmem')),'Failed writes committed a save'
                                failure.unlink()
                            if args.failure and len(frames)==70: keys([0x14,0x2d])
                            if args.failure and len(frames)==75: keys([])
                            if len(frames)==150:
                                core.write_text('MENU\n'); frozen.append(bytes(shared)); return
                        time.sleep(.001)
                except BaseException as error: failures.append(error)
            thread=threading.Thread(target=emulate); thread.start()
            env=os.environ.copy(); env['LD_PRELOAD']=str(preload); env['TM_TEST_PMEM_SLOW_DIR']=str(saves)+'/'
            if args.failure: env['TM_TEST_PMEM_FAIL_FILE']=str(failure)
            # Permit the filesystem shim before libasan when this is an ASAN
            # fixture. This does not disable address instrumentation.
            env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':verify_asan_link_order=0'
            process=subprocess.Popen([args.frontend,'--folder',str(folder),'--saves',str(saves),
                '--cart',str(cart),'--run','--memory',str(memory),'--core-name',str(core)],
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
            try:
                output=process.communicate(timeout=15)[0]
                assert process.returncode==0,output
                assert not failures,failures
                assert frozen and bytes(shared)==frozen[0],'DDR changed after core departure'
                recoveries=1 if args.failure else 0
                assert f'recoveries={recoveries} ' in output and 'departed=1 error=0' in output,output
                if args.failure: assert 'identity transition could not finish' in output,output
                assert output.count('Injected 500 ms persistent-memory fsync delay')==1,output
                assert clock.underruns==0,(clock.underruns,output)
                waiting=int(re.search(r'waiting_frames=(\d+)',output)[1]); assert waiting>=10,(waiting,output)
                completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                files=list(saves.glob('*.pmem')); assert len(files)==2,([p.name for p in files],output)
                counts=[]
                for path in files:
                    saved=path.read_bytes()
                    assert len(saved)==1036 and saved[:4]==b'TMPM'
                    assert struct.unpack_from('<I',saved,4)[0]==1
                    assert struct.unpack_from('<I',saved,8)[0]==zlib.crc32(saved[12:])
                    counts.append(struct.unpack_from('<I',saved,12)[0])
                if args.failure:
                    assert all(count>0 for count in counts) and max(counts)>min(counts),(counts,output)
                else: assert sum(counts)==completed+1,(counts,completed,output)
                assert not list(saves.glob('*.tmp-*'))
                for worker in map(int,re.findall(r'worker=(\d+)',output)):
                    assert not Path('/proc/'+str(worker)).exists(),'Worker survived exit'
                print(f'Studio slow save transport: 500 ms fsync, {waiting} paced waiting frames, zero modeled DAC underruns, two CRC-valid identities, acknowledged sum={sum(counts)}, recoveries={recoveries}, failure_retry={args.failure}')
            finally:
                if process.poll() is None: process.kill(); process.communicate(timeout=5)
                stop.set(); thread.join(timeout=3); assert not thread.is_alive()
