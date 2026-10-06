"""Drive the actual supervised Studio frontend through paced DDR and input."""
import argparse
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import struct
import signal
import re
import subprocess
import tempfile
import threading
import time
import zlib
from audio_fixture import AudioClock

parser=argparse.ArgumentParser()
parser.add_argument('--frontend',required=True)
parser.add_argument('--scenario',choices=('console','run','hang','kill','save','read-error'))
parser.add_argument('--fft-fixture',action='store_true')
args=parser.parse_args()
if args.fft_fixture: assert args.scenario=='run'
protocol=json.loads((Path(__file__).resolve().parents[1]/'protocol/memory_map.json').read_text())
offsets=protocol['offsets']
scenarios={'console':False,'run':True,'hang':'hang','kill':'kill','save':'save','read-error':'read-error'}
for game in ((scenarios[args.scenario],) if args.scenario else scenarios.values()):
    with tempfile.TemporaryDirectory(prefix='tic80-studio-live-') as directory:
        root=Path(directory); folder=root/'studio'; folder.mkdir()
        saves=root/'saves'; saves.mkdir()
        core=root/'CORENAME'; core.write_text('TIC-80\n')
        memory=root/'ddr'
        with memory.open('w+b') as file:
            file.truncate(protocol['region_bytes'])
            with mmap.mmap(file.fileno(),protocol['region_bytes']) as shared:
                def get(name): return struct.unpack_from('<I',shared,offsets[name])[0]
                def put(name,value): ctypes.c_uint32.from_buffer(shared,offsets[name]).value=value&0xffffffff
                put('IDENTITY',protocol['magic'])
                put('GEOMETRY',protocol['width']|protocol['height']<<16)
                put('KEYBOARD',2); put('MOUSE',120|68<<8)
                command=[args.frontend,'--folder',str(folder),'--saves',str(saves),'--memory',str(memory),'--core-name',str(core)]
                env=None
                if args.fft_fixture:
                    command+=['--fft-device','Mic A']
                    env=dict(os.environ,TM_TEST_FFT_LOG=str(root/'capture-events'))
                if game:
                    code=b'function TIC() cls(btn(4) and 12 or 6) if key(23) then cls(11) end end\n'
                    if args.fft_fixture:
                        code=b'function TIC() cls(btn(4) and 12 or 6) if key(23) then cls(11) end if math.floor(fftr(32)+0.5)~=512 then cls(3) end end\n'
                    if game=='hang':
                        code=b'function TIC() cls(6) if btn(4) then mset(0,0,77) pmem(0,99) while true do end end end\n'
                    if game in ('save','read-error'):
                        code=b'-- saveid: studio-live-shared-save\nfunction TIC() cls(6) pmem(0,pmem(0)+1) pmem(255,0x89abcdef) end\n'
                    if game=='read-error':
                        key=hashlib.md5(b'studio-live-shared-save').hexdigest()
                        damaged=saves/(key+'.pmem'); damaged.write_bytes(b'corrupt save must survive rejection')
                    cart=root/'input.tic'
                    cart.write_bytes(bytes([17,0,0,0,5,len(code),0,0])+code)
                    command+=['--cart',str(cart),'--run']
                stop=threading.Event(); frames=[]; failures=[]; frozen=[]; clocks=[]
                def emulate():
                    try:
                        session=presented=0; clock=AudioClock()
                        clocks.append(clock)
                        while not stop.is_set():
                            put('HEARTBEAT',get('HEARTBEAT')+1)
                            request=get('SESSION_REQUEST')
                            if request!=session:
                                session=request; presented=0; clock.reset()
                                put('VIDEO_PRESENTED',0); put('AUDIO_READ',0); put('SESSION_ACK',session)
                            put('AUDIO_READ',clock.sample(get('AUDIO_WRITE')))
                            pub=get('VIDEO_PUBLISH')
                            if session and pub&2 and pub!=presented:
                                offset=offsets['BUFFER1'] if pub&1 else offsets['BUFFER0']
                                picture=bytes(shared[offset:offset+protocol['frame_bytes']])
                                assert all(alpha==255 for alpha in picture[3::4]),'Invalid RGBA alpha'
                                # Keep the color oracle away from Studio's
                                # mouse cursor, initially at (128,72).
                                center=(32*256+32)*4
                                frames.append((picture[center:center+4],zlib.crc32(picture)))
                                assert picture==bytes(shared[offset:offset+protocol['frame_bytes']]),'Pending frame overwritten'
                                put('VIDEO_PRESENTED',pub); presented=pub
                                count=len(frames)
                                if game:
                                    if count==25: put('JOY0',1<<4)
                                    if count==50: put('JOY0',0)
                                    if count==70: put('JOY0',1<<8) # remapped W, keyboard-only
                                    if count==95: put('JOY0',0)
                                if game=='kill' and count==25:
                                    children=Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
                                    workers=[int(child) for child in children if Path(f'/proc/{child}/comm').read_text().strip()=='tic80-studio']
                                    assert len(workers)==1,workers
                                    os.kill(workers[0],signal.SIGKILL)
                                if game=='read-error':
                                    if count==90:
                                        assert damaged.read_bytes()==b'corrupt save must survive rejection'
                                        values=[0]*256; values[0]=100
                                        payload=struct.pack('<256I',*values)
                                        damaged.write_bytes(b'TMPM'+struct.pack('<II',1,zlib.crc32(payload))+payload)
                                    if count in (70,75,100,105):
                                        sequence=get('KEYBOARD'); put('KEYBOARD',sequence+1)
                                        words=[0]*16
                                        if count in (70,100): words[0]=1<<0x14; words[1]=1<<(0x2d-32)
                                        struct.pack_into('<16I',shared,offsets['KEYBOARD_BITS'],*words)
                                        put('KEYBOARD',sequence+2)
                                if count==(180 if game in ('hang','kill') else 130 if game else 90):
                                    core.write_text('MENU\n'); frozen.append(bytes(shared)); return
                            time.sleep(.001)
                    except BaseException as error:
                        failures.append(error)
                thread=threading.Thread(target=emulate); thread.start()
                process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
                try:
                    output=process.communicate(timeout=18)[0]
                    assert process.returncode==0,output
                    # Core-name notification can wake the frontend before this
                    # recorder resumes from write_text. Wait for the emulator
                    # to finish its departure snapshot before inspecting it.
                    thread.join(timeout=3)
                    assert not thread.is_alive(),('Departure recorder did not finish',game,len(frames),output)
                    assert not failures,failures
                    current=bytes(shared)
                    if frozen and current!=frozen[0]:
                        changed=[i for i,(before,after) in enumerate(zip(frozen[0],current)) if before!=after]
                        raise AssertionError(('DDR was written after departure',game,len(changed),
                            [(hex(i),frozen[0][i],current[i]) for i in changed[:24]],output))
                    assert frozen,'No departure snapshot'
                    assert 'departed=1 error=0' in output,output
                    for worker in map(int,re.findall(r'worker=(\d+)',output)):
                        try: os.kill(worker,0)
                        except ProcessLookupError: pass
                        else: raise AssertionError('Studio worker survived frontend exit')
                    assert get('AUDIO_WRITE')>=len(frames)*800
                    assert any(crc for _,crc in frames)
                    assert clocks[0].underruns==0,(clocks[0].underruns,output)
                    if args.fft_fixture:
                        assert 'Studio microphone capture: active' in output,output
                        assert 'microphone capture unavailable' not in output,output
                        events=(root/'capture-events').read_text().splitlines()
                        opened=[line for line in events if ' device-open ' in line]
                        closed=[line for line in events if ' device-close ' in line]
                        assert len(opened)==1 and len(closed)<=1,events
                        assert all(int(line.split()[0])!=process.pid for line in events),events
                        assert not any(' device-busy ' in line for line in events),events
                        for worker in {int(line.split()[0]) for line in opened}:
                            try: os.kill(worker,0)
                            except ProcessLookupError: pass
                            else: raise AssertionError('Capture worker survived departure')
                        # Departure can cancel a pending tick with SIGKILL.
                        # Prove the OS released the same exclusive device even
                        # when no orderly ma_device_uninit event was possible.
                        import fcntl
                        with (root/'capture-events.A.lock').open('r+b') as device_lock:
                            fcntl.flock(device_lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                            fcntl.flock(device_lock,fcntl.LOCK_UN)
                        print('Studio CLI capture: worker-owned input, nonzero FFT in RUN, paced playback and clean departure passed')
                    if game in ('save','read-error'):
                        key=hashlib.md5(b'studio-live-shared-save').hexdigest()
                        saved=(saves/(key+'.pmem')).read_bytes()
                        assert len(saved)==1036 and saved[:4]==b'TMPM'
                        assert struct.unpack_from('<I',saved,4)[0]==1
                        assert struct.unpack_from('<I',saved,8)[0]==zlib.crc32(saved[12:])
                        values=struct.unpack_from('<256I',saved,12)
                        completed=int(re.search(r'completed_ticks=(\d+)',output)[1])
                        if game=='save': assert values[0]==1+completed,(values[0],output)
                        else:
                            assert 100<values[0]<200,(values[0],output)
                            assert output.count('Studio RUN save rejected:')==2 and 'recoveries=0 ' in output,output
                            assert {pixel for pixel,_ in frames[110:125]}!={pixel for pixel,_ in frames[20:50]},'RUN retry stayed in console'
                            print('Studio read rejection stayed live at startup and physical Ctrl+R, preserved corruption, and accepted the repaired save')
                        assert values[255]==0x89abcdef
                        assert not any(path.is_file() and path.name==key for path in folder.rglob('*')),'Worker wrote an upstream raw save'
                        print('Studio frontend saved exactly its acknowledged ticks in the shared TMPM format')
                    if game is True:
                        colors=lambda start,end:{pixel for pixel,_ in frames[start:end]}
                        base=colors(8,20); button=colors(30,45); key=colors(75,90)
                        assert len(base)==len(button)==len(key)==1,(base,button,key)
                        assert base!=button and base!=key and button!=key,(base,button,key)
                        assert colors(55,65)==base and colors(105,120)==base,'Input release failed'
                    if game in ('hang','kill'):
                        assert 'recoveries=1 ' in output,output
                        assert len(re.findall('Studio worker recovered:',output))==1,output
                        if game=='hang':
                            assert int(re.search(r'waiting_frames=(\d+)',output)[1])>=10,output
                        assert 'mode=1' in output,'Recovery did not return to console'
                        print(str(game)+' recovery maintained the modeled DAC with zero underruns')
                    print(('RUN/input' if game else 'Console')+' live transport and clean core departure passed')
                finally:
                    if process.poll() is None: process.kill(); process.communicate(timeout=5)
                    stop.set(); thread.join(timeout=3)
                    assert not thread.is_alive()
