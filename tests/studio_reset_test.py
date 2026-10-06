"""Paced Studio reset hold/release, held OSD selection and held departure."""
import argparse
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from audio_fixture import AudioClock

parser=argparse.ArgumentParser(); parser.add_argument('--frontend',required=True)
parser.add_argument('--scenario',choices=('switch','load','repeat','departure','initial'),required=True); args=parser.parse_args()
scenario=args.scenario
protocol=json.loads((Path(__file__).resolve().parents[1]/'protocol/memory_map.json').read_text()); offsets=protocol['offsets']
def cart(name,color):
    code=(f'-- saveid: reset-{name}\nfunction BOOT() pmem(1,pmem(1)+1) end '
          f'function TIC() cls({color}) pmem(0,pmem(0)+1) end\n').encode()
    return bytes((17,0,0,0,5,len(code)&255,len(code)>>8,0))+code
def values(path):
    data=path.read_bytes()
    assert len(data)==1036 and data[:4]==b'TMPM' and struct.unpack_from('<I',data,8)[0]==zlib.crc32(data[12:]),path
    return struct.unpack_from('<256I',data,12)
with tempfile.TemporaryDirectory(prefix='tic80-studio-reset-') as directory:
    root=Path(directory); folder=root/'studio'; folder.mkdir(); saves=root/'saves'; saves.mkdir()
    core=root/'CORENAME'; core.write_text('TIC-80\n'); memory=root/'ddr'; initial=root/'a.tic'; initial.write_bytes(cart('a',6))
    paths={}
    for name,start in [('a',10),('b',100)]:
        data=[0]*256; data[0]=start; payload=struct.pack('<256I',*data)
        path=saves/(hashlib.md5(('reset-'+name).encode()).hexdigest()+'.pmem'); paths[name]=path
        path.write_bytes(b'TMPM'+struct.pack('<II',1,zlib.crc32(payload))+payload)
    with memory.open('w+b') as file:
        file.truncate(protocol['region_bytes'])
        with mmap.mmap(file.fileno(),protocol['region_bytes']) as shared:
            def get(name): return struct.unpack_from('<I',shared,offsets[name])[0]
            def put(name,value): ctypes.c_uint32.from_buffer(shared,offsets[name]).value=value&0xffffffff
            put('IDENTITY',protocol['magic']); put('GEOMETRY',protocol['width']|protocol['height']<<16); put('KEYBOARD',2)
            if scenario=='initial': put('STATUS',1)
            stop=threading.Event(); failures=[]; frames=[]; frozen=[]; checkpoints=[]; clock=AudioClock()
            finish=80 if scenario=='departure' else 170
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
                            picture=bytes(shared[offset:offset+protocol['frame_bytes']]); assert all(a==255 for a in picture[3::4])
                            frames.append(zlib.crc32(picture)); put('VIDEO_PRESENTED',publication); presented=publication
                            count=len(frames)
                            if (count==30 and scenario!='initial') or (scenario=='repeat' and count==90): put('STATUS',1)
                            if scenario=='initial' and count==55 and values(paths['a'])[:2]!=(10,0):
                                failures.append('Startup BOOT ran before reset release')
                            if (scenario=='switch' and count==50) or (scenario=='load' and count==29):
                                payload=cart('b',12); put('CART_META',5)
                                struct.pack_into('<I',shared,offsets['CART_META']+4,len(payload))
                                shared[offsets['CART_DATA']:offsets['CART_DATA']+len(payload)]=payload; put('CART_META',6)
                            if count==45 or (scenario=='repeat' and count==105): checkpoints.append(values(paths['a']))
                            if count==55 or (scenario=='repeat' and count==115):
                                if values(paths['a'])!=checkpoints[-1]: failures.append('A executed while reset was held')
                            if scenario in ('switch','load') and count==80:
                                if values(paths['b'])[:2]!=(100,0): failures.append('B booted before reset release')
                                if get('CART_ACK')!=6: failures.append('Held OSD ticket was not privately acknowledged')
                            if scenario in ('switch','load') and count==90: put('STATUS',0)
                            if scenario=='repeat' and count in (60,120): put('STATUS',0)
                            if scenario=='initial' and count==60: put('STATUS',0)
                            if count==finish:
                                core.write_text('MENU\n'); frozen.append(bytes(shared)); return
                        time.sleep(.001)
                except BaseException as error: failures.append(error)
            thread=threading.Thread(target=emulate); thread.start()
            process=subprocess.Popen([args.frontend,'--folder',str(folder),'--saves',str(saves),'--cart',str(initial),'--run',
                '--memory',str(memory),'--core-name',str(core)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            try:
                output=process.communicate(timeout=15)[0]
                assert process.returncode==0,output
                assert not failures,(failures,output)
                assert frozen and bytes(shared)==frozen[0],'DDR changed after departure'
                assert clock.underruns==0,(clock.underruns,output)
                assert 'departed=1 error=0' in output,output
                a=values(paths['a']); b=values(paths['b'])
                if scenario=='repeat':
                    completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                    assert a[1]==3 and a[0]==10+1+2+completed,(a[:2],completed,output)
                    assert 'Studio reset: holds=2 runs=2' in output,output
                elif scenario in ('switch','load'):
                    assert a[1]==1 and b[1]==1 and b[0]>100,(a[:2],b[:2],output)
                    assert 'Studio reset: holds=1 runs=0' in output and 'requests=1 loaded=1 rejected=0' in output,output
                elif scenario=='initial':
                    completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                    assert a[1]==1 and a[0]==10+1+completed,(a[:2],completed,output)
                    assert 'Studio reset: holds=1 runs=1' in output,output
                else:
                    assert a[1]==1 and b[:2]==(100,0),(a[:2],b[:2],output)
                    assert 'Studio reset: holds=1 runs=0' in output,output
                assert not list(saves.glob('*.tmp-*'))
                for pid in set(map(int,re.findall(r'worker=(\d+)',output))): assert not Path('/proc/'+str(pid)).exists()
                print(output)
                print(f'Studio reset {scenario}: execution held, acknowledged pmem preserved, exact BOOT count, zero modeled DAC underruns and clean departure')
            finally:
                if process.poll() is None: process.kill(); process.communicate(timeout=5)
                stop.set(); thread.join(timeout=3); assert not thread.is_alive()
