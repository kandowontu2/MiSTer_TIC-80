"""Run a bounded Studio transport test through a temporary one-shot handler."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import re
import time
import uuid
import paramiko
from hardware_ssh import command, connect

root=Path(__file__).resolve().parents[1]; build=root/'build'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fault',choices=('hang',))
args=parser.parse_args()
prefix='studio-live-'+uuid.uuid4().hex[:8]
remote='/tmp/tic80-mister-dev/'+prefix
handler='/media/fat/games/TIC-80/_handler.sh'
known_handler='7c317c3fc4879f77827bc3b753ba34467fea4446341f8da296ba89d8613f3461'
installed=json.loads((build/'video-clock-installed.json').read_text())
fixture=build/'arm/tic80-studio-live'
sha=lambda data:hashlib.sha256(data).hexdigest()
result=dict(prefix=prefix,installed=installed,fixture_sha256=sha(fixture.read_bytes()),passed=False)
result['fault']=args.fault
client=paramiko.SSHClient(); client.load_system_host_keys()
connect(client,'192.168.1.176',username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)
changed=False; wrapper_sha=None; original=None; initial=None
def selected():
    core=command(client,'cat /tmp/CORENAME').strip()
    assert core in ('TIC-80','MENU'),'Another core selected'
    return core
def mutate(text):
    core=selected()
    return command(client,'test "$(cat /tmp/CORENAME)" = '+core+' && { '+text+'; }')
def load_core(path): mutate('printf "load_core '+path+'\\n" > /dev/MiSTer_cmd')
def wait_menu():
    deadline=time.monotonic()+20
    while selected()!='MENU' or command(client,'pidof TIC-80 || true').strip():
        assert time.monotonic()<deadline,'Own player did not stop'
        time.sleep(.2)
try:
    initial=selected()
    qualification=json.loads((build/'studio-local-qualification.json').read_text())
    assert qualification['local_checks_passed']
    assert qualification['fixtures']['arm/tic80-studio-live']==result['fixture_sha256'],'ARM fixture differs from the locally checked build'
    for name,digest in qualification['compiled_sources'].items():
        assert sha((root/name).read_bytes())==digest,'Source changed after local qualification: '+name
    for path,digest in installed.items(): assert command(client,'sha256sum '+path).split()[0]==digest
    with client.open_sftp() as sftp:
        with sftp.open(handler,'rb') as file: original=file.read()
    assert sha(original)==known_handler,'Review the modified launcher before replacing it'
    cart='/media/fat/games/TIC-80/Carts/tetris.tic'
    result['cart_sha256']=command(client,'sha256sum '+cart).split()[0]
    assert result['cart_sha256']=='b2090c8279cec7e7a31adecbdf44498ac4d02da8fb3b0f11e021305983b8bdbe'
    mutate('mkdir -p '+remote+'/studio '+remote+'/saves')
    result['shared_save_directory']=remote+'/saves'
    fault_cart=None
    if args.fault:
        code=b'local n=0 function TIC() n=n+1 cls(6) if n==180 then mset(0,0,77) sync(4,7,true) pmem(0,99) while true do end end end\n'
        fault_cart=bytes([17,0,0,0,5,len(code),0,0])+code
        cart=remote+'/fault.tic'; result['cart_sha256']=sha(fault_cart)
    wrapper=('#!/bin/sh\nsleep 1\nif mkdir '+remote+'/once 2>/dev/null; then\n'
        '  '+remote+'/tic80-studio-live --folder '+remote+'/studio --saves '+remote+'/saves --cart '+cart+' --run --pulse 16 --ticks 900 > '+remote+'/frontend.log 2>&1\n'
        '  printf "%s\\n" "$?" > '+remote+'/frontend.status\nfi\n'
        'exec /bin/sh '+remote+'/original-handler.sh\n').encode()
    wrapper_sha=sha(wrapper)
    with client.open_sftp() as sftp:
        sftp.put(str(fixture),remote+'/tic80-studio-live')
        sftp.put(str(root/'assets/cacert.pem'),remote+'/cacert.pem')
        if fault_cart:
            with sftp.open(cart,'wb') as file: file.write(fault_cart)
        for path,payload in ((remote+'/original-handler.sh',original),(remote+'/handler.sh',wrapper)):
            with sftp.open(path,'wb') as file: file.write(payload)
    assert command(client,'sha256sum '+remote+'/tic80-studio-live').split()[0]==result['fixture_sha256']
    if selected()=='TIC-80': load_core('/media/fat/menu.rbf')
    wait_menu()
    mutate('chmod +x '+remote+'/tic80-studio-live; cp '+remote+'/handler.sh '+handler+'.new; chmod +x '+handler+'.new')
    assert command(client,'sha256sum '+handler+'.new').split()[0]==wrapper_sha
    mutate('mv '+handler+'.new '+handler+'; sync'); changed=True
    assert command(client,'sha256sum '+handler).split()[0]==wrapper_sha
    load_core('/media/fat/_Other/TIC80_20260930.rbf')
    deadline=time.monotonic()+40
    while True:
        selected()
        log=command(client,'cat '+remote+'/frontend.log 2>/dev/null || true')
        if 'TIC-80 Studio live ready: mode=2 ' in log: break
        assert 'error=1' not in log,log
        assert time.monotonic()<deadline,'Studio never became ready: '+log
        time.sleep(.2)
    assert selected()=='TIC-80'
    raw=command(client,'test "$(cat /tmp/CORENAME)" = TIC-80 && /tmp/tic80-mister-dev/runtime-monitor --seconds 10 --interval-ms 20',timeout=20)
    (build/(prefix+'.jsonl')).write_text(raw)
    rows=[json.loads(line) for line in raw.splitlines()]
    assert len(rows)>400 and len({row['session'] for row in rows})==1
    result.update(samples=len(rows),first=rows[0],last=rows[-1],underruns=max(row['underruns'] for row in rows))
    deadline=time.monotonic()+15
    while True:
        assert selected()=='TIC-80'
        status=command(client,'cat '+remote+'/frontend.status 2>/dev/null || true').strip()
        if status: break
        assert time.monotonic()<deadline,'Bounded frontend has not stopped'
        time.sleep(.2)
    log=command(client,'cat '+remote+'/frontend.log')
    (build/(prefix+'.log')).write_text(log)
    assert status=='0',log
    match=re.search(r'Studio live stopped: ticks=(\d+) recoveries=(\d+) mean_ms=([\d.]+) max_ms=([\d.]+) over_budget=(\d+) wall_seconds=([\d.]+) departed=(\d+) error=(\d+)',log)
    assert match,log
    values=match.groups()
    result['frontend']=dict(ticks=int(values[0]),recoveries=int(values[1]),mean_ms=float(values[2]),max_ms=float(values[3]),over_budget=int(values[4]),wall_seconds=float(values[5]),departed=int(values[6]),error=int(values[7]))
    assert result['frontend']['ticks']==900 and result['frontend']['recoveries']==int(bool(args.fault)) and not result['frontend']['error']
    async_match=re.search(r'Studio async: completed_ticks=(\d+) waiting_frames=(\d+) first_waiting_frame=(\d+)',log)
    assert async_match,log
    result['async']=dict(completed_ticks=int(async_match[1]),waiting_frames=int(async_match[2]),first_waiting_frame=int(async_match[3]))
    if args.fault:
        assert result['async']['waiting_frames']>=10 and 'Studio worker recovered:' in log
        assert re.search(r'Studio worker recovered: worker=\d+ mode=1',log),log
    else:
        assert result['async']['waiting_frames']==0,result
        assert result['async']['completed_ticks']==899,result
    assert rows[-1]['written']>rows[0]['written'] and rows[-1]['played']>rows[0]['played']
    assert rows[-1]['presented']>rows[0]['presented']
    assert result['underruns']==0,result
    result['passed']=True
finally:
    try:
        if changed and sha(command(client,'cat '+handler).encode())==wrapper_sha:
            mutate('cp '+remote+'/original-handler.sh '+handler+'.new; chmod +x '+handler+'.new; mv '+handler+'.new '+handler+'; sync')
            assert command(client,'sha256sum '+handler).split()[0]==known_handler
            result['handler_restored']=True
        if initial=='TIC-80' and changed and selected() in ('TIC-80','MENU'):
            load_core('/tmp/tic80-mister-dev/controller-keys/tetris.mgl')
            result['tetris_restoration_requested']=True
    finally:
        (build/(prefix+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        (build/'studio-live-native.json').write_text(json.dumps(result,indent=2)+'\n')
        client.close()
print(json.dumps(result,indent=2))
