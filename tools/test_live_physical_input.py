"""Load or inspect a human-operated input diagnostic; never injects input.

The user must physically operate the devices. A fresh UUID save prevents prior
input from satisfying the check. --inspect leaves the cartridge running.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import zlib
import paramiko
from hardware_ssh import connect

ROOT = Path(__file__).resolve().parents[1]
folder = ROOT / 'build/physical-input'
p = argparse.ArgumentParser()
p.add_argument('--host', required=True)
p.add_argument('--inspect', action='store_true')
p.add_argument('--seconds', type=int, default=180)
a = p.parse_args()
assert 1 <= a.seconds <= 600
c = paramiko.SSHClient()
c.load_system_host_keys()
if (ROOT/'build/ssh_known_hosts').exists():
    c.load_host_keys(str(ROOT/'build/ssh_known_hosts'))
connect(c,a.host,username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)


def command(text):
    _,out,err = c.exec_command(text,timeout=15)
    output,errors = out.read().decode(),err.read().decode()
    if out.channel.recv_exit_status(): raise RuntimeError(errors or output or text)
    return output


def core(): return command('cat /tmp/CORENAME').strip()


def supervisor():
    return command('for p in $(pidof TIC-80); do '
        'test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && printf "%s " "$p"; '
        'done; true').strip()


def guard():
    if core() != 'TIC-80' or supervisor() != parent:
        raise RuntimeError('Core or supervisor changed during physical input test')


def read_save(sftp):
    with sftp.open(savepath,'rb') as file: data = file.read()
    assert len(data)==1036 and data[:4]==b'TMPM'
    assert struct.unpack_from('<I',data,8)[0]==zlib.crc32(data[12:])
    words = struct.unpack_from('<256I',data,12)
    assert words[0]==1, 'Diagnostic restarted; generate a fresh cartridge'
    return dict(boots=words[0],controller_seen=words[1],controller_held=words[2],
        mouse_seen=words[3],mouse_held=words[4],wheel_up=words[5],wheel_down=words[6],
        mouse_x=words[8],mouse_y=words[9],x_changes=words[10],y_changes=words[11],
        released_ticks=words[12],ticks=words[13],controller_presses=list(words[20:28]))


try:
    hashes = {}
    for name,local,remote in [('player','build/arm/tic80-live','/media/fat/games/TIC-80/TIC-80'),
                             ('rbf','build/fpga/output_files/TIC80.rbf','/media/fat/_Other/TIC80_20260930.rbf')]:
        hashes[name] = hashlib.sha256((ROOT/local).read_bytes()).hexdigest()
        assert command('sha256sum '+remote).split()[0]==hashes[name]
    saveid = (folder/'saveid.txt').read_text().strip()
    assert saveid.startswith('tic80-physical-') and all(x.isalnum() or x=='-' for x in saveid)
    savepath = '/media/fat/saves/TIC-80/'+hashlib.md5(saveid.encode()).hexdigest()+'.pmem'
    with c.open_sftp() as sftp:
        if not a.inspect:
            if core() not in ('MENU','TIC-80'): raise RuntimeError('Another core selected')
            try: sftp.stat(savepath)
            except FileNotFoundError: pass
            else: raise RuntimeError('Generate a fresh diagnostic before loading')
            remote = '/tmp/tic80-mister-dev/physical-input'
            command('mkdir -p '+remote)
            cart = folder/'diagnostic.tic'
            sftp.put(str(cart),remote+'/diagnostic.tic')
            with sftp.open(remote+'/diagnostic.mgl','w') as file:
                file.write('<mistergamedescription>\n <rbf>_Other/TIC80</rbf>\n'
                    f' <file delay="3" type="f" index="0" path="{remote}/diagnostic.tic"/>\n'
                    '</mistergamedescription>\n')
            oldpid = supervisor()
            before = command('cat /media/fat/logs/TIC-80/tic80.log 2>/dev/null || true')
            selected = core()
            if selected not in ('MENU','TIC-80'): raise RuntimeError('Another core selected')
            command(f'test "$(cat /tmp/CORENAME)" = "{selected}" || exit 30; '
                f'printf "load_core {remote}/diagnostic.mgl\\n" > /dev/MiSTer_cmd')
            marker = f'Cartridge loaded: {cart.stat().st_size} bytes; reset=0'
            deadline = time.monotonic()+40
            while True:
                selected = core()
                if selected not in ('MENU','TIC-80'): raise RuntimeError('Another core selected')
                parent = supervisor()
                log = command('cat /media/fat/logs/TIC-80/tic80.log 2>/dev/null || true')
                if selected=='TIC-80' and parent.isdigit() and marker in log and (
                    log.count(marker)>before.count(marker) or parent!=oldpid): break
                if time.monotonic()>deadline: raise RuntimeError(log)
                time.sleep(.25)
            time.sleep(2)
            guard()
            result = read_save(sftp)
            assert result['controller_seen']==0 and result['mouse_seen']==0, result
            inventory = command('cat /proc/bus/input/devices')
            (folder/'device-inventory.txt').write_text(inventory)
            (folder/'loaded.json').write_text(json.dumps(dict(hashes=hashes,saveid=saveid,
                supervisor_pid=parent,initial=result),indent=2)+'\n')
            print('Physical input diagnostic ready: '+json.dumps(result),flush=True)
        else:
            parent = supervisor()
            assert parent.isdigit()
            loaded = json.loads((folder/'loaded.json').read_text())
            assert loaded['hashes']==hashes and loaded['saveid']==saveid and loaded['supervisor_pid']==parent
            deadline = time.monotonic()+a.seconds
            while True:
                guard()
                result = read_save(sftp)
                complete = (result['controller_seen']==255 and result['mouse_seen']==7
                    and result['wheel_up']>0 and result['wheel_down']>0
                    and result['x_changes']>0 and result['y_changes']>0
                    and result['controller_held']==0 and result['mouse_held']==0
                    and result['released_ticks']>=120)
                print(json.dumps(result),flush=True)
                (folder/'result.json').write_text(json.dumps(dict(hashes=hashes,saveid=saveid,
                    supervisor_pid=parent,complete=complete,observations=result),indent=2)+'\n')
                if complete: break
                if time.monotonic()>deadline: raise RuntimeError('Physical input checks remain incomplete')
                time.sleep(3)
            print('Physical controller/mouse diagnostic passed; no input was injected',flush=True)
finally:
    c.close()
