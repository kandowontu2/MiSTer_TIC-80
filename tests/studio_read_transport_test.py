"""Pace the actual frontend through one-shot worker save-read stalls."""
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
parser.add_argument('--scenario',choices=('slow','timeout','run-timeout','departure'),required=True)
args=parser.parse_args()
protocol=json.loads((Path(__file__).resolve().parents[1]/'protocol/memory_map.json').read_text())
offsets=protocol['offsets']; scenario=args.scenario
running=scenario=='run-timeout'; departure=scenario=='departure'; slow=scenario=='slow'
trigger=10 if running else 30
retry=80 if running else 400
finish=500 if scenario=='timeout' else 150
duration=500 if slow else 10000
expected_recoveries=0 if slow or departure else 1

with tempfile.TemporaryDirectory(prefix='tic80-studio-read-transport-') as directory:
    root=Path(directory); folder=root/'studio'; folder.mkdir(); saves=root/'saves'; saves.mkdir()
    core=root/'CORENAME'; core.write_text('TIC-80\n')
    arm=root/'arm-read'; entered=root/'entered-read'; memory=root/'ddr'
    # The first TIC changes bank0's identity. A later Ctrl+R must read the new
    # identity; restarting an unchanged identity would just use its last ACK.
    code=b"function TIC() pmem(0,pmem(0)+1) cls(6) mset(0,0,42) sync(4,0,true) sfx(0,'C-4',-1,0,15) end\n"
    def chunk(kind,payload): return bytes((kind,len(payload)&255,len(payload)>>8,0))+payload
    cart=root/'cart.tic'
    cart.write_bytes(bytes((17,0,0,0))+chunk(10,bytes(8)+bytes([255])*8)+chunk(5,code))
    preload=root/'read.so'; shutil.copyfile(args.fault_library,preload)
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
            def last_pcm():
                start=(get('AUDIO_WRITE')-800)&0xffffffff
                index=start%protocol['audio_capacity']; first=min(800,protocol['audio_capacity']-index)
                offset=offsets['AUDIO_RING']+index*4
                return bytes(shared[offset:offset+first*4])+bytes(shared[offsets['AUDIO_RING']:offsets['AUDIO_RING']+(800-first)*4])
            put('IDENTITY',protocol['magic']); put('GEOMETRY',protocol['width']|protocol['height']<<16); put('KEYBOARD',2)
            stop=threading.Event(); failures=[]; frames=[]; frozen=[]; clock=AudioClock(); departure_time=[]
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
                            assert all(alpha==255 for alpha in picture[3::4]),'Invalid RGBA frame'
                            frames.append((zlib.crc32(picture),any(last_pcm())))
                            put('VIDEO_PRESENTED',publication); presented=publication
                            count=len(frames)
                            if not running and count==10: keys([0x76]) # physical Esc returns to console
                            if not running and count==15: keys([])
                            if count==trigger: arm.touch(); keys([0x14,0x2d])
                            if count==trigger+5: keys([])
                            if not slow and not departure and count==retry:
                                assert len(list(saves.glob('*.pmem')))==1,'Stalled RUN persisted an unacknowledged identity'
                                keys([0x14,0x2d])
                            if not slow and not departure and count==retry+5: keys([])
                            if count==finish:
                                core.write_text('MENU\n'); frozen.append(bytes(shared)); departure_time.append(time.monotonic()); return
                        time.sleep(.001)
                except BaseException as error: failures.append(error)
            thread=threading.Thread(target=emulate); thread.start()
            env=os.environ.copy(); env.update(LD_PRELOAD=str(preload),TM_TEST_PMEM_READ_DIR=str(saves)+'/',
                TM_TEST_PMEM_READ_ARM=str(arm),TM_TEST_PMEM_READ_ENTERED=str(entered),TM_TEST_PMEM_READ_MS=str(duration))
            env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':verify_asan_link_order=0'
            process=subprocess.Popen([args.frontend,'--folder',str(folder),'--saves',str(saves),'--cart',str(cart),
                '--run','--memory',str(memory),'--core-name',str(core)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
            try:
                output=process.communicate(timeout=20)[0]
                assert process.returncode==0,output
                assert not failures,failures
                assert frozen and bytes(shared)==frozen[0],'DDR changed after core departure'
                assert time.monotonic()-departure_time[0]<2,'Closing a blocked read waited for its injected ten-second delay'
                assert f'recoveries={expected_recoveries} ' in output and 'departed=1 error=0' in output,output
                assert output.count(f'Injected {duration} ms Studio save read delay:')==1,output
                assert entered.exists() and not arm.exists(),'Worker did not consume the one-shot read fault'
                waiting=int(re.search(r'waiting_frames=(\d+)',output)[1])
                assert waiting>=(250 if scenario=='timeout' else 70 if departure else 10),(waiting,output)
                assert clock.underruns==0,(clock.underruns,output)
                pending=frames[trigger+3:trigger+9] if running or slow else frames[50:100]
                assert len({crc for crc,_ in pending})==1,'Pending read changed the acknowledged picture'
                assert all(not pcm for _,pcm in pending),'Pending read replayed old PCM instead of submitting silence'
                assert any(pcm for _,pcm in frames[3:9]),'Initial RUN did not provide a nonzero tone'
                if not departure: assert any(pcm for _,pcm in frames[-20:-5]),'Successful load/retry did not resume the tone'
                files=list(saves.glob('*.pmem')); assert len(files)==(1 if departure else 2),([p.name for p in files],output)
                counts=[]
                for path in files:
                    saved=path.read_bytes()
                    assert len(saved)==1036 and saved[:4]==b'TMPM'
                    assert struct.unpack_from('<I',saved,4)[0]==1 and struct.unpack_from('<I',saved,8)[0]==zlib.crc32(saved[12:])
                    counts.append(struct.unpack_from('<I',saved,12)[0])
                assert all(count>0 for count in counts),counts
                assert not list(saves.glob('*.tmp-*'))
                workers={int(entered.read_text())}|set(map(int,re.findall(r'worker=(\d+)',output)))
                for pid in workers: assert not Path('/proc/'+str(pid)).exists(),'Worker survived exit'
                print(output)
                print(f'Studio read transport {scenario}: {duration} ms injected delay, {waiting} waiting frames, zero modeled DAC underruns, private picture retained, silent pending PCM, {len(files)} CRC-valid identities, recoveries={expected_recoveries}, clean departure')
            finally:
                if process.poll() is None: process.kill(); process.communicate(timeout=5)
                stop.set(); thread.join(timeout=3); assert not thread.is_alive()
