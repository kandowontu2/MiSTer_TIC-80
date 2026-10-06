"""Validate Xbox keyboard bindings through synthetic Linux input and live TIC APIs."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import struct
import time
import uuid
import zlib
import paramiko
from hardware_ssh import command as execute, connect

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix',default='controller-keys',choices=('controller-keys','controller-qe'))
args=parser.parse_args(); prefix=args.prefix
client=paramiko.SSHClient(); client.load_system_host_keys()
connect(client,'192.168.1.176',username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)
command=lambda text:execute(client,text)
core=lambda:command('cat /tmp/CORENAME').strip()
def guard(): assert core() in ('MENU','TIC-80'),'Another core selected'
def parent():
    return command('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()
saveid='tic80-controller-'+uuid.uuid4().hex
code=f'''-- script: lua
-- saveid: {saveid}
local keys={{23,1,19,4,50,66,17,5}}
function BOOT() pmem(0,pmem(0)+1) end
function TIC()
 cls(0)
 local mask=0
 for i,k in ipairs(keys) do
  if key(k) then mask=mask+(1<<(i-1)); pmem(19+i,pmem(19+i)+1) end
  if keyp(k) then pmem(9+i,pmem(9+i)+1) end
 end
 pmem(1,mask); pmem(2,pmem(2)|mask); pmem(3,btn()); pmem(5,pmem(5)+1)
 if mask~=0 and (btn()&240)~=0 then pmem(6,pmem(6)+1) end
 if mask==3 and btn()==8 then pmem(7,pmem(7)+1) end
 if mask==0 and btn()==0 then pmem(8,pmem(8)+1) else pmem(8,0) end
 print("CONTROLLER KEYBOARD TEST",10,10,12)
 print("WASD / ENTER / ESC / Q / E",10,24,12)
 print("keys: "..mask.."  pad: "..btn(),10,38,12)
end
'''.encode()
cart=bytes([5])+struct.pack('<H',len(code))+b'\0'+code
folder=ROOT/'build'/(prefix+'-live'); folder.mkdir(exist_ok=True)
(folder/'diagnostic.tic').write_bytes(cart)
remote='/tmp/tic80-mister-dev/controller-keys'
savepath='/media/fat/saves/TIC-80/'+hashlib.md5(saveid.encode()).hexdigest()+'.pmem'
def load(path,sftp):
    guard(); before=command('cat /media/fat/logs/TIC-80/tic80.log 2>/dev/null || true'); previous=parent()
    size=len(cart) if path.endswith('/diagnostic.mgl') else sftp.stat('/media/fat/games/TIC-80/Carts/tetris.tic').st_size
    marker=f'Cartridge loaded: {size} bytes; reset=0'
    selected=core(); assert selected in ('MENU','TIC-80')
    command(f'test "$(cat /tmp/CORENAME)" = "{selected}" && printf "load_core {path}\\n" > /dev/MiSTer_cmd')
    deadline=time.monotonic()+40
    while True:
        guard(); log=command('cat /media/fat/logs/TIC-80/tic80.log 2>/dev/null || true')
        if core()=='TIC-80' and parent() and marker in log and (log.count(marker)>before.count(marker) or parent()!=previous): break
        assert time.monotonic()<deadline,log
        time.sleep(.25)
    time.sleep(2)
try:
    guard()
    installed=json.loads((ROOT/'build'/(prefix+'-installed.json')).read_text())
    for path,sha in installed['hashes'].items(): assert command('sha256sum '+path).split()[0]==sha,path
    command('mkdir -p '+remote)
    with client.open_sftp() as sftp:
        sftp.put(str(ROOT/'build/arm/input_test'),remote+'/input-test')
        command('chmod +x '+remote+'/input-test')
        native=command('nice -n 19 taskset 1 '+remote+'/input-test')
        assert 'key/keyp and release' in native
        (folder/'native-input.log').write_text(native)
        sftp.put(str(ROOT/'build/arm/tic80-controller-probe'),remote+'/probe')
        command('chmod +x '+remote+'/probe')
        sftp.put(str(folder/'diagnostic.tic'),remote+'/diagnostic.tic')
        for name,path in [('diagnostic',remote+'/diagnostic.tic'),('tetris','/media/fat/games/TIC-80/Carts/tetris.tic')]:
            with sftp.open(remote+'/'+name+'.mgl','w') as f:
                f.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                        f' <file delay="3" type="f" index="0" path="{path}"/>\n</mistergamedescription>\n')
        load(remote+'/diagnostic.mgl',sftp)
        supervisor=parent(); assert supervisor.isdigit()
        probe=command('test "$(cat /tmp/CORENAME)" = TIC-80 && '+remote+'/probe')
        (folder/'probe.log').write_text(probe)
        time.sleep(3); assert core()=='TIC-80' and parent()==supervisor
        with sftp.open(savepath,'rb') as f: data=f.read()
        assert len(data)==1036 and data[:4]==b'TMPM'
        assert struct.unpack_from('<I',data,8)[0]==zlib.crc32(data[12:])
        values=struct.unpack_from('<256I',data,12)
        assert values[0]==1 and values[1]==0 and values[2]==255 and values[3]==0,values[:28]
        assert values[6]==0 and values[7]>0 and values[8]>120,values[:28]
        assert list(values[10:18])==[2,2,1,1,1,1,1,1],values[:28]
        assert all(value>0 for value in values[20:28]),values[:28]
        result=dict(hashes=installed['hashes'],saveid=saveid,supervisor=supervisor,
                    probe=probe,pmem=list(values[:28]),passed=True,
                    scope='Synthetic Xbox events through Main/HPS/DDR and live Lua; physical controller pending')
        (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        load(remote+'/tetris.mgl',sftp)
        print('Native input and live remapper/API checks passed; Tetris restored')
finally: client.close()
