"""Full Studio transport reload must wait for Main, preserving accepted state."""
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
parser.add_argument('--trace-library',required=True)
parser.add_argument('--early-initialization-negative-control',action='store_true')
parser.add_argument('--scenario',choices=('delayed','generation','prior','departure','offline','absent','queued','timeout','repeat'),required=True)
args=parser.parse_args(); scenario=args.scenario
assert not args.early_initialization_negative_control or scenario=='queued'
protocol=json.loads((Path(__file__).resolve().parents[1]/'protocol/memory_map.json').read_text()); offsets=protocol['offsets']
def cart(name,color):
    code=(f'-- saveid: reload-{name}\nfunction BOOT() pmem(1,pmem(1)+1) end '
          f'function TIC() pmem(0,pmem(0)+1) cls({color}) trace("[[reload-tick:{name}:"..pmem(0).."]]") end\n').encode()
    return bytes((17,0,0,0,5,len(code)&255,len(code)>>8,0))+code
def values(path):
    data=path.read_bytes()
    assert len(data)==1036 and data[:4]==b'TMPM' and struct.unpack_from('<I',data,8)[0]==zlib.crc32(data[12:])
    return struct.unpack_from('<256I',data,12)
with tempfile.TemporaryDirectory(prefix='tic80-studio-reload-') as directory:
    root=Path(directory); folder=root/'studio'; folder.mkdir(); saves=root/'saves'; saves.mkdir()
    core=root/'CORENAME'; core.write_text('TIC-80\n'); memory=root/'ddr'; initial=root/'a.tic'; initial.write_bytes(cart('a',6))
    paths={}
    for name in ('a','b'):
        payload=bytes(1024); path=saves/(hashlib.md5(('reload-'+name).encode()).hexdigest()+'.pmem'); paths[name]=path
        path.write_bytes(b'TMPM'+struct.pack('<II',1,zlib.crc32(payload))+payload)
    with memory.open('w+b') as file:
        file.truncate(protocol['region_bytes'])
        with mmap.mmap(file.fileno(),protocol['region_bytes']) as shared:
            def get(name): return struct.unpack_from('<I',shared,offsets[name])[0]
            def put(name,value): ctypes.c_uint32.from_buffer(shared,offsets[name]).value=value&0xffffffff
            put('IDENTITY',protocol['magic']); put('GEOMETRY',protocol['width']|protocol['height']<<16); put('KEYBOARD',2)
            stop=threading.Event(); failures=[]; frames=[]; frozen=[]; clocks=[]; offline_frozen=[]
            finish=80 if scenario=='departure' else (250 if scenario=='repeat' else 170)
            def emulate():
                try:
                    session=presented=0; clock=AudioClock(); clocks.append(clock); offline_until=0; offline_snapshot=None; running_picture=None
                    def reset():
                        nonlocal session,presented,clock,offline_until,offline_snapshot
                        session=presented=0
                        put('SESSION_ACK',0); put('STATUS',0)
                        if scenario in ('offline','absent'):
                            put('IDENTITY',0); offline_until=time.monotonic()+(.25 if scenario=='offline' else 30); offline_snapshot=None
                    while not stop.is_set():
                        if offline_until:
                            if time.monotonic()<offline_until:
                                if scenario=='absent' or time.monotonic()>offline_until-.22:
                                    if offline_snapshot is None: offline_snapshot=bytes(shared); offline_frozen.append(offline_snapshot)
                                    elif bytes(shared)!=offline_snapshot: raise AssertionError('DDR mutated while FPGA identity was absent')
                                time.sleep(.001); continue
                            put('IDENTITY',protocol['magic']); offline_until=0
                        put('HEARTBEAT',get('HEARTBEAT')+1)
                        request=get('SESSION_REQUEST')
                        # Fresh FPGA ignores the old ARM request until the
                        # parent chooses a new nonce. ACK=0 exposes session loss.
                        if request!=get('SESSION_ACK') and request!=last_request[0]:
                            session=request; last_request[0]=request; presented=0
                            clock=AudioClock(); clocks.append(clock)
                            put('VIDEO_PRESENTED',0); put('AUDIO_READ',0); put('SESSION_ACK',session)
                        if session:
                            put('AUDIO_READ',clock.sample(get('AUDIO_WRITE')))
                            publication=get('VIDEO_PUBLISH')
                            if publication&2 and publication!=presented:
                                offset=offsets['BUFFER1'] if publication&1 else offsets['BUFFER0']
                                picture=bytes(shared[offset:offset+protocol['frame_bytes']]); assert all(a==255 for a in picture[3::4])
                                frames.append(zlib.crc32(picture)); put('VIDEO_PRESENTED',publication); presented=publication
                                count=len(frames)
                                if count==10: running_picture=picture
                                if count==40 and scenario=='prior': put('STATUS',1)
                                if count==50 or (scenario=='repeat' and count==150): reset()
                                if count==65 or (scenario=='repeat' and count==165):
                                    live_output.write(b'[[hold-start]]\n')
                                if count==70 and scenario in ('delayed','prior','offline','queued','repeat'): put('STATUS',1)
                                if count==170 and scenario=='repeat': put('STATUS',1)
                                if count==75 or (scenario=='repeat' and count==175):
                                    if picture==running_picture: failures.append('RUN picture published before initialization')
                                if scenario=='queued' and count==72:
                                    payload=cart('b',12); put('CART_META',5)
                                    struct.pack_into('<I',shared,offsets['CART_META']+4,len(payload))
                                    shared[offsets['CART_DATA']:offsets['CART_DATA']+len(payload)]=payload; put('CART_META',6)
                                if scenario=='queued' and count==85:
                                    if get('CART_ACK')!=6 or values(paths['b'])[:2]!=(0,0): failures.append('Queued selection booted early or lost ACK')
                                if args.early_initialization_negative_control and count==80: put('STATUS',0)
                                if count==90 and scenario not in ('departure','timeout'):
                                    live_output.write(b'[[hold-end]]\n')
                                    if scenario!='generation': put('STATUS',0)
                                    if scenario!='delayed': core.write_text('TIC-80\n')
                                if count==190 and scenario=='repeat':
                                    live_output.write(b'[[hold-end]]\n'); put('STATUS',0); core.write_text('TIC-80\n')
                                if count==finish and scenario!='timeout':
                                    core.write_text('MENU\n'); frozen.append(bytes(shared)); return
                        time.sleep(.001)
                except BaseException as error: failures.append(error)
            # Disk writes are asynchronous: a pause ACK can flush earlier TIC
            # values during this interval. Observe actual execution instead.
            # All writers append to one log, so buffered output cannot reorder
            # cartridge traces across the model's hold boundary markers.
            live_output=(root/'live-output').open('a+b',buffering=0)
            last_request=[0]; thread=threading.Thread(target=emulate); thread.start()
            process=subprocess.Popen([args.frontend,'--folder',str(folder),'--saves',str(saves),'--cart',str(initial),'--run',
                '--memory',str(memory),'--core-name',str(core)],stdout=live_output,stderr=subprocess.STDOUT,
                env=dict(os.environ,LD_PRELOAD=str(Path(args.trace_library).resolve())))
            try:
                process.wait(timeout=18); output=(root/'live-output').read_text()
                assert re.search(r'\[\[reload-tick:a:\d+\]\]',output),'Cartridge execution trace missing'
                for held in output.split('[[hold-start]]')[1:]:
                    held=held.split('[[hold-end]]',1)[0]
                    assert not re.search(r'\[\[reload-tick:[ab]:\d+\]\]',held), 'Cartridge TIC ran before Main initialization'
                assert not failures,(failures,output)
                a=values(paths['a']); b=values(paths['b'])
                assert sum(clock.underruns for clock in clocks)==0,([c.underruns for c in clocks],output)
                if scenario in ('timeout','absent'):
                    assert process.returncode==1 and 'initialization did not complete' in output,output
                    assert a[1]==1,(a[:2],output)
                    completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                    assert a[0]==1+completed,(a[:2],completed,output)
                    if scenario=='absent': assert bytes(shared)==offline_frozen[0],output
                else:
                    assert process.returncode==0 and 'departed=1 error=0' in output,output
                    assert frozen and bytes(shared)==frozen[0],'DDR changed after departure'
                    if scenario=='departure':
                        completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                        assert a[1]==1 and a[0]==1+completed,(a[:2],completed,output)
                    elif scenario=='queued':
                        assert a[1]==1 and b[1]==1 and b[0]>0,(a[:2],b[:2],output)
                        completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                        assert a[0]+b[0]==2+completed,(a[:2],b[:2],completed,output)
                        assert 'requests=1 loaded=1 rejected=0' in output,output
                    else:
                        boots=3 if scenario=='repeat' else 2
                        completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                        assert a[1]==boots and a[0]==boots+completed,(a[:2],completed,output)
                expected=2 if scenario=='repeat' else 1
                assert f'Studio FPGA: reloads={expected}' in output,output
                ready=0 if scenario in ('departure','timeout','absent') else expected
                assert output.count('Studio MiSTer initialization ready')==ready,output
                assert not list(saves.glob('*.tmp-*'))
                for pid in set(map(int,re.findall(r'worker=(\d+)',output))): assert not Path('/proc/'+str(pid)).exists()
                print(output)
                print(f'Studio reload {scenario}: last ACK, initialization gate, exact BOOT count, pmem CRC and worker cleanup passed')
            finally:
                if process.poll() is None: process.kill(); process.wait(timeout=5)
                stop.set(); thread.join(timeout=3); assert not thread.is_alive()
                live_output.close()
