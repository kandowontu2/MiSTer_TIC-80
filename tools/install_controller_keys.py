"""Install the timing-checked controller menu/player and Xbox default profiles."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import time
import paramiko
from controller_defaults import create,decode
from hardware_access import require_access
from hardware_ssh import command as execute, connect

ROOT=Path(__file__).resolve().parents[1]
HOST='192.168.1.176'
require_access(HOST)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix',default='controller-keys',choices=('controller-keys','controller-qe'))
args=parser.parse_args(); prefix=args.prefix
BASE=ROOT/'build'/(prefix+'-baseline')
baseline=json.loads((BASE/'video-clock-installed.json').read_text())
record=json.loads((ROOT/'build/fpga-build.json').read_text())
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rbf='/media/fat/_Other/TIC80_20260930.rbf'
player='/media/fat/games/TIC-80/TIC-80'
for name,sha in record['sources'].items(): assert digest(ROOT/name)==sha, name
for name,sha in record['post_fit_audits'].items(): assert digest(ROOT/'build'/name)==sha,name
assert digest(ROOT/'build/fpga/output_files/TIC80.rbf')==record['rbf_sha256']
assert not record['enable_yc']
assert '100% tests passed' in (ROOT/'build'/(prefix+'-tests-final.log')).read_text()
assert 'key/keyp and release' in (ROOT/'build'/(prefix+'-arm-tests-final.log')).read_text()
payloads={rbf:ROOT/'build/fpga/output_files/TIC80.rbf',player:ROOT/'build/arm/tic80-live'}
system=(BASE/'input_045e_028e_v3.map').read_bytes()
for identity in ('045e_028e','20d6_2062'):
    name=f'TIC-80_input_{identity}_v3.map'
    existing=BASE/name
    output=ROOT/'build'/name
    profile=system
    if identity=='20d6_2062':
        # This controller has no saved global map; its standard shoulder
        # bindings are BTN_TL/BTN_TR, matching Main's global defaults.
        import struct
        values=decode(system); values[8:10]=[0x136,0x137]
        profile=struct.pack('<32I',*values)
    output.write_bytes(create(profile,existing.read_bytes() if existing.exists() else None))
    payloads['/media/fat/config/inputs/'+name]=output
client=paramiko.SSHClient(); client.load_system_host_keys()
connect(client,HOST,username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)
command=lambda text:execute(client,text)
core=lambda:command('cat /tmp/CORENAME').strip()
def guard(): assert core() in ('MENU','TIC-80'),'Another core selected'
def parent():
    return command('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()
try:
    guard()
    monitor='/tmp/tic80-mister-dev/runtime-monitor'
    if command('if test -f '+monitor+'; then printf present; fi').strip()!='present':
        local=ROOT/'build/arm/tic80-runtime-monitor'
        assert digest(local)==baseline[monitor]
        selected=core(); assert selected in ('MENU','TIC-80')
        command(f'test "$(cat /tmp/CORENAME)" = "{selected}" && mkdir -p /tmp/tic80-mister-dev')
        with client.open_sftp() as sftp: sftp.put(str(local),monitor)
        command('chmod +x '+monitor)
    for path,sha in baseline.items(): assert command('sha256sum '+path).split()[0]==sha,path
    with client.open_sftp() as sftp:
        try:
            sftp.stat('/media/fat/config/inputs/input_20d6_2062_v3.map')
        except FileNotFoundError:
            pass
        else:
            raise AssertionError('Xbox global map now exists; refresh its shoulder defaults before installation')
        with sftp.open('/media/fat/config/inputs/input_045e_028e_v3.map','rb') as f:
            assert f.read()==system,'Global Xbox map changed; refresh the local baseline'
        with sftp.open('/media/fat/config/inputs/TIC-80_input_20d6_2062_v3.map','rb') as f:
            assert f.read()==(BASE/'TIC-80_input_20d6_2062_v3.map').read_bytes(),'TIC-80 map changed; refresh the baseline'
        sftp.get(player,str(BASE/'TIC-80'))
        assert digest(BASE/'TIC-80')==baseline[player]
        if core()=='TIC-80':
            command('test "$(cat /tmp/CORENAME)" = TIC-80 && printf "load_core /media/fat/menu.rbf\\n" > /dev/MiSTer_cmd')
        deadline=time.monotonic()+20
        while core()!='MENU' or parent():
            guard(); assert time.monotonic()<deadline; time.sleep(.2)
        backup='/media/fat/games/TIC-80/.rollback-controller-keys'
        command('test "$(cat /tmp/CORENAME)" = MENU && mkdir -p '+backup)
        changes=[]
        for remote,local in payloads.items():
            assert core()=='MENU'
            before=None
            try:
                with sftp.open(remote,'rb') as f: old=f.read()
                before=hashlib.sha256(old).hexdigest()
                rollback=backup+'/'+Path(remote).name+'.'+before[:12]
                try: sftp.stat(rollback)
                except FileNotFoundError:
                    with sftp.open(rollback,'wb') as f: f.write(old)
                assert command('sha256sum '+rollback).split()[0]==before
            except FileNotFoundError: rollback=None
            stage='/tmp/tic80-mister-dev/controller-keys-'+Path(remote).name
            sftp.put(str(local),stage)
            assert command('sha256sum '+stage).split()[0]==digest(local)
            executable='chmod +x '+remote+'.new; ' if remote==player else ''
            command('set -e; test "$(cat /tmp/CORENAME)" = MENU; cp '+stage+' '+remote+'.new; '
                    +executable+'sync; test "$(cat /tmp/CORENAME)" = MENU; mv '+remote+'.new '+remote+'; sync')
            assert command('sha256sum '+remote).split()[0]==digest(local)
            changes.append(dict(path=remote,sha256=digest(local),previous=before,rollback=rollback))
        installed=dict(baseline)
        for path in (rbf,player): installed[path]=digest(payloads[path])
        for name in ('video-clock-installed.json','lifecycle-installed.json','audio-pacing-installed.json'):
            (ROOT/'build'/name).write_text(json.dumps(installed,indent=2)+'\n')
        result=dict(hashes=installed,changes=changes,fpga=record,
                    qualification='Local timing and input tests passed; live remapper/HDMI checks pending')
        (ROOT/'build'/(prefix+'-installed.json')).write_text(json.dumps(result,indent=2)+'\n')
        print('Controller menu, ARM player and two Xbox profiles installed; backups retained')
finally: client.close()
