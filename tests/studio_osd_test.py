"""Real Studio frontend: OSD ownership, switching, rejection and busy queues."""
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

parser=argparse.ArgumentParser(); parser.add_argument('--frontend',required=True); args=parser.parse_args()
protocol=json.loads((Path(__file__).resolve().parents[1]/'protocol/memory_map.json').read_text()); offsets=protocol['offsets']
def cart(name,color,hang=False):
    code=(f'-- saveid: osd-{name}\nfunction BOOT() pmem(1,pmem(1)+1) end '
        f'function TIC() cls({color}) pmem(0,pmem(0)+1) '+
        ('if btn(4) then pmem(0,0xdeadbeef) while true do end end ' if hang else '')+'end\n').encode()
    return bytes((17,0,0,0,5,len(code)&255,len(code)>>8,0))+code
def png_cart(native):
    def chunk(kind,payload): return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload))
    pixels=(b'\0'+bytes((0,0,0,255))*32)*32
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',32,32,8,6,0,0,0))+
        chunk(b'IDAT',zlib.compress(pixels))+chunk(b'caRt',zlib.compress(native))+chunk(b'IEND',b''))
with tempfile.TemporaryDirectory(prefix='tic80-studio-osd-') as directory:
    root=Path(directory); folder=root/'studio'; folder.mkdir(); saves=root/'saves'; saves.mkdir()
    core=root/'CORENAME'; core.write_text('TIC-80\n'); memory=root/'ddr'
    starts={'a':10,'b':100,'hung':1000,'latest':10000}
    for name,value in starts.items():
        values=[0]*256; values[0]=value; payload=struct.pack('<256I',*values)
        key=hashlib.md5(('osd-'+name).encode()).hexdigest()
        (saves/(key+'.pmem')).write_bytes(b'TMPM'+struct.pack('<II',1,zlib.crc32(payload))+payload)
    with memory.open('w+b') as file:
        file.truncate(protocol['region_bytes'])
        with mmap.mmap(file.fileno(),protocol['region_bytes']) as shared:
            def get(name): return struct.unpack_from('<I',shared,offsets[name])[0]
            def put(name,value): ctypes.c_uint32.from_buffer(shared,offsets[name]).value=value&0xffffffff
            def transfer(ticket,payload):
                if get('CART_ACK')!=previous[0]:
                    failures.append(('prior ticket never acknowledged',ticket,get('CART_ACK'),previous[0])); return
                put('CART_META',(ticket&~3)|1)
                struct.pack_into('<I',shared,offsets['CART_META']+4,len(payload))
                shared[offsets['CART_DATA']:offsets['CART_DATA']+len(payload)]=payload
                put('CART_META',ticket); previous[0]=ticket; sent.append(ticket)
            put('IDENTITY',protocol['magic']); put('GEOMETRY',protocol['width']|protocol['height']<<16); put('KEYBOARD',2)
            previous=[0]; sent=[]; acknowledged=[]; frames=[]; frozen=[]; failures=[]; clock=AudioClock(); stop=threading.Event()
            def emulate():
                try:
                    session=presented=0; last_ack=0
                    while not stop.is_set():
                        put('HEARTBEAT',get('HEARTBEAT')+1)
                        request=get('SESSION_REQUEST')
                        if request!=session:
                            session=request; presented=0; clock.reset()
                            put('VIDEO_PRESENTED',0); put('AUDIO_READ',0); put('SESSION_ACK',session)
                        put('AUDIO_READ',clock.sample(get('AUDIO_WRITE')))
                        ack=get('CART_ACK')
                        if ack!=last_ack:
                            acknowledged.append(ack); last_ack=ack
                            # The bridge can reclaim staging immediately after
                            # ACK; neither the queued cart nor IPC may alias it.
                            shared[offsets['CART_DATA']:offsets['CART_DATA']+32]=b'x'*32
                        publication=get('VIDEO_PUBLISH')
                        if session and publication&2 and publication!=presented:
                            offset=offsets['BUFFER1'] if publication&1 else offsets['BUFFER0']
                            picture=bytes(shared[offset:offset+protocol['frame_bytes']])
                            assert all(alpha==255 for alpha in picture[3::4])
                            center=(32*protocol['width']+32)*4
                            frames.append(picture[center:center+4]); put('VIDEO_PRESENTED',publication); presented=publication
                            count=len(frames)
                            if count==10: transfer(6,cart('a',6))
                            if count==65: transfer(10,png_cart(cart('b',12)))
                            if count==125: transfer(14,b'junk')
                            if count==165: transfer(19,b'') # bridge-rejected transfer
                            if count==205: transfer(22,cart('hung',9,True))
                            if count==255: put('JOY0',1<<4)
                            if count==260: transfer(26,cart('superseded',5))
                            if count==265: transfer(30,cart('latest',8))
                            if count==280: put('JOY0',0)
                            if count==340:
                                core.write_text('MENU\n'); frozen.append(bytes(shared)); return
                        time.sleep(.001)
                except BaseException as error: failures.append(error)
            thread=threading.Thread(target=emulate); thread.start()
            process=subprocess.Popen([args.frontend,'--folder',str(folder),'--saves',str(saves),'--memory',str(memory),
                '--core-name',str(core)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            try:
                output=process.communicate(timeout=15)[0]
                assert process.returncode==0,output
                assert not failures,(failures,output)
                assert sent==acknowledged==[6,10,14,19,22,26,30],(sent,acknowledged,output)
                assert 'Studio OSD: requests=7 loaded=4 rejected=2 superseded=1' in output,output
                assert 'recoveries=1 ' in output and 'departed=1 error=0' in output,output
                assert frozen and bytes(shared)==frozen[0],'DDR changed after departure'
                assert clock.underruns==0,(clock.underruns,output)
                colors=lambda first,last:set(frames[first:last])
                phases=[colors(30,55),colors(90,115),colors(220,240),colors(305,335)]
                assert all(len(phase)==1 for phase in phases) and len(set(next(iter(p)) for p in phases))==4,phases
                assert colors(150,160)==colors(180,195)==phases[1],'Rejected cart changed the current game'
                for name,initial in starts.items():
                    key=hashlib.md5(('osd-'+name).encode()).hexdigest(); saved=(saves/(key+'.pmem')).read_bytes()
                    assert len(saved)==1036 and saved[:4]==b'TMPM' and struct.unpack_from('<I',saved,8)[0]==zlib.crc32(saved[12:])
                    values=struct.unpack_from('<256I',saved,12)
                    assert initial<values[0]<initial+340 and values[1]==1,(name,values[:2],output)
                assert not (saves/(hashlib.md5(b'osd-superseded').hexdigest()+'.pmem')).exists(),'Superseded cart ran'
                assert not list(saves.glob('*.tmp-*'))
                for pid in set(map(int,re.findall(r'worker=(\d+)',output))): assert not Path('/proc/'+str(pid)).exists()
                print(output)
                print('Studio OSD: seven tickets privately acknowledged, native/PNG carts, four correct games, malformed/failed transfers preserved the game, latest busy queue won, no partial hung PMEM, zero modeled DAC underruns and clean departure')
            finally:
                if process.poll() is None: process.kill(); process.communicate(timeout=5)
                stop.set(); thread.join(timeout=3); assert not thread.is_alive()
