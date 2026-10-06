"""OSD selections use the actual Studio dialog and physical input converter."""
import argparse
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from audio_fixture import AudioClock
parser=argparse.ArgumentParser(); parser.add_argument('--frontend',required=True)
parser.add_argument('--scenario',choices=('cancel','accept','latest','reset','crash','escape','departure'),required=True)
parser.add_argument('--source-packets',action='store_true')
args=parser.parse_args(); scenario=args.scenario
protocol=json.loads((Path(__file__).resolve().parents[1]/'protocol/memory_map.json').read_text()); offsets=protocol['offsets']
def cart(name,color):
    code=(f'-- saveid: selection-{name}\nfunction BOOT() pmem(1,pmem(1)+1) end '
          f'function TIC() cls({color}) pmem(0,pmem(0)+1) end\n').encode()
    return bytes((17,0,0,0,5,len(code)&255,len(code)>>8,0))+code
def code(data):
    position=0; result=b''
    while position+4<=len(data):
        kind=data[position]&31; size=int.from_bytes(data[position+1:position+3],'little'); position+=4
        payload=data[position:position+size]; position+=size
        if kind==5: result+=payload
        elif kind==16: result+=zlib.decompress(payload)
    return result.rstrip(b'\0')
with tempfile.TemporaryDirectory(prefix='tic80-studio-select-') as directory:
    root=Path(directory); folder=root/'studio'; folder.mkdir(); saves=root/'saves'; saves.mkdir()
    core=root/'CORENAME'; core.write_text('TIC-80\n'); memory=root/'ddr'; initial=root/'a.tic'; original=cart('a',6); initial.write_bytes(original)
    with memory.open('w+b') as file:
        file.truncate(protocol['region_bytes'])
        with mmap.mmap(file.fileno(),protocol['region_bytes']) as shared:
            def get(name): return struct.unpack_from('<I',shared,offsets[name])[0]
            def put(name,value): ctypes.c_uint32.from_buffer(shared,offsets[name]).value=value&0xffffffff
            def keys(*pressed):
                sequence=get('KEYBOARD'); put('KEYBOARD',sequence+1); words=[0]*16
                for key in pressed: words[key//32]|=1<<(key%32)
                struct.pack_into('<16I',shared,offsets['KEYBOARD_BITS'],*words); put('KEYBOARD',sequence+2)
            def transfer(ticket,name,color):
                payload=cart(name,color); put('CART_META',(ticket&~3)|1)
                if args.source_packets:
                    directory=root/f'candidate-{ticket}'; directory.mkdir(); source=directory/'cart.tic'; source.write_bytes(payload)
                    path=os.fsencode(source)+b'\0'; assert len(path)<=protocol['cart_source_capacity']
                    struct.pack_into('<2I',shared,offsets['CART_SOURCE'],ticket,len(path))
                    shared[offsets['CART_SOURCE']+8:offsets['CART_SOURCE']+8+len(path)]=path
                struct.pack_into('<I',shared,offsets['CART_META']+4,len(payload))
                shared[offsets['CART_DATA']:offsets['CART_DATA']+len(payload)]=payload; put('CART_META',ticket)
            put('IDENTITY',protocol['magic']); put('GEOMETRY',protocol['width']|protocol['height']<<16); put('KEYBOARD',2)
            if args.source_packets: struct.pack_into('<I',shared,offsets['IDENTITY']+4,protocol['cart_source_magic'])
            stop=threading.Event(); failures=[]; frames=[]; frozen=[]; clock=AudioClock(); acknowledged=[]
            finish=75 if scenario=='departure' else (210 if scenario in ('reset','crash') else 165)
            def emulate():
                try:
                    session=presented=last_ack=0
                    while not stop.is_set():
                        put('HEARTBEAT',get('HEARTBEAT')+1); request=get('SESSION_REQUEST')
                        if request!=session:
                            session=request; presented=0; clock.reset(); put('VIDEO_PRESENTED',0); put('AUDIO_READ',0); put('SESSION_ACK',session)
                        put('AUDIO_READ',clock.sample(get('AUDIO_WRITE')))
                        ack=get('CART_ACK')
                        if ack!=last_ack:
                            acknowledged.append(ack); last_ack=ack
                            shared[offsets['CART_DATA']:offsets['CART_DATA']+32]=b'x'*32
                            if args.source_packets:
                                shared[offsets['CART_SOURCE']:offsets['CART_SOURCE']+8+protocol['cart_source_capacity']]=b'x'*(8+protocol['cart_source_capacity'])
                        publication=get('VIDEO_PUBLISH')
                        if session and publication&2 and publication!=presented:
                            offset=offsets['BUFFER1'] if publication&1 else offsets['BUFFER0']; picture=bytes(shared[offset:offset+protocol['frame_bytes']])
                            assert all(a==255 for a in picture[3::4]); frames.append(zlib.crc32(picture)); put('VIDEO_PRESENTED',publication); presented=publication
                            count=len(frames)
                            if count==15: keys(0x05) # F1 editor
                            if count==20: keys()
                            if count==25: keys(0x14,0x169) # Ctrl+End
                            if count==30: keys()
                            if count==35: keys(0x29) # one unsaved trailing space
                            if count==40: keys()
                            if count==50: transfer(6,'b',12)
                            if count==75:
                                if scenario=='latest': transfer(10,'c',9)
                                elif scenario=='reset': put('STATUS',1)
                                elif scenario=='crash':
                                    children=Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split(); assert len(children)==1,children
                                    os.kill(int(children[0]),signal.SIGKILL)
                            if scenario=='reset' and count==95: put('STATUS',0)
                            slow=scenario in ('reset','crash')
                            if count==(125 if slow else 80):
                                if scenario=='escape': put('JOY0',1<<13) # Back -> ESC
                                elif scenario=='cancel': put('JOY0',1<<4) # A selects default NO
                                else: put('JOY0',1<<2) # MiSTer Down -> YES
                            if count==(129 if slow else 84): put('JOY0',0)
                            if count==(145 if slow else 100) and scenario not in ('cancel','escape'): put('JOY0',1<<4)
                            if count==(149 if slow else 104): put('JOY0',0)
                            if scenario in ('cancel','escape') and count==110: keys(0x14,0x1b) # save retained edits
                            if count==115: keys()
                            if count==finish:
                                core.write_text('MENU\n'); frozen.append(bytes(shared)); return
                        time.sleep(.001)
                except BaseException as error: failures.append(error)
            thread=threading.Thread(target=emulate); thread.start()
            process=subprocess.Popen([args.frontend,'--folder',str(folder),'--saves',str(saves),'--cart',str(initial),
                '--memory',str(memory),'--core-name',str(core)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            try:
                output=process.communicate(timeout=15)[0]; assert process.returncode==0,output
                assert not failures,(failures,output); assert frozen and bytes(shared)==frozen[0],'DDR changed after departure'
                assert clock.underruns==0,(clock.underruns,output)
                assert acknowledged==([6,10] if scenario=='latest' else [6]),(acknowledged,output)
                prompts=2 if scenario in ('reset','crash') else 1
                cancelled=1 if scenario in ('cancel','escape') else 0
                assert f'Studio selection: prompts={prompts} cancelled={cancelled}' in output,output
                if scenario in ('cancel','escape'):
                    saved=initial; assert saved.exists(),(list(folder.iterdir()),output)
                    assert code(saved.read_bytes())==code(original)+b' ',code(saved.read_bytes())
                    assert not (folder/'a.tic').exists(),'Save lost the CLI source path'
                    assert 'requests=1 loaded=0 rejected=0 superseded=0' in output,output
                    assert not list(saves.glob('*.pmem')),'Cancelled cart ran'
                elif scenario=='departure': assert not list(saves.glob('*.pmem')),'Pending cart ran during departure'
                else:
                    name='c' if scenario=='latest' else 'b'; key=hashlib.md5(('selection-'+name).encode()).hexdigest()
                    data=(saves/(key+'.pmem')).read_bytes()
                    assert data[:4]==b'TMPM' and struct.unpack_from('<I',data,8)[0]==zlib.crc32(data[12:])
                    values=struct.unpack_from('<256I',data,12); assert values[0]>1 and values[1]==1,(values[:2],output)
                    assert len(list(saves.glob('*.pmem')))==1,'Superseded candidate booted'
                    superseded=1 if scenario=='latest' else 0
                    assert f'loaded=1 rejected=0 superseded={superseded}' in output,output
                    if args.source_packets:
                        ticket=10 if scenario=='latest' else 6
                        assert f'MiSTer cartridge selected: {root}/candidate-{ticket}/cart.tic' in output,output
                assert not list(saves.glob('*.tmp-*'))
                for pid in set(map(int,re.findall(r'worker=(\d+)',output))): assert not Path('/proc/'+str(pid)).exists()
                print(output); print(f'Studio selection transport {scenario}: unsaved edits, physical dialog input, candidate ownership, pmem, source packets={args.source_packets} and departure passed')
            finally:
                if process.poll() is None: process.kill(); process.communicate(timeout=5)
                stop.set(); thread.join(timeout=3); assert not thread.is_alive()
