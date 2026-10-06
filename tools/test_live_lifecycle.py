"""Guarded hardware reload/MENU stress with a fresh, CRC-checked counter cart.

Exercises the installed Frontier launch path. Never interrupts a different
core, changes the shared daemon, or injects controller events. Ends in MENU.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import time
import uuid
import zlib
import paramiko
from hardware_process import descriptors
from hardware_ssh import command as ssh_command, connect

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--host',required=True)
p.add_argument('--cycles',type=int,default=4)
p.add_argument('--reloads',type=int,default=16)
a=p.parse_args()
assert 1<=a.cycles<=32 and 1<=a.reloads<=16
c=paramiko.SSHClient(); c.load_system_host_keys()
if (ROOT/'build/ssh_known_hosts').exists(): c.load_host_keys(str(ROOT/'build/ssh_known_hosts'))
connect(c,a.host,username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)
folder=ROOT/'build/lifecycle'
folder.mkdir(parents=True,exist_ok=True)
records=[]


def command(text):
    return ssh_command(c,text)


def core(): return command('cat /tmp/CORENAME').strip()


def guard():
    if core() not in ('TIC-80','MENU'): raise RuntimeError('Another core selected during lifecycle test')


def parent():
    return command('for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()


def log(): return command('cat /media/fat/logs/TIC-80/tic80.log 2>/dev/null || true')


def load(path,allowed):
    if core()!=allowed: raise RuntimeError('Core changed before load')
    command(f'test "$(cat /tmp/CORENAME)" = "{allowed}" || exit 30; '
            f'printf "load_core {path}\\n" > /dev/MiSTer_cmd')


def save(sftp):
    with sftp.open(savepath,'rb') as file: data=file.read()
    assert len(data)==1036 and data[:4]==b'TMPM'
    assert struct.unpack_from('<I',data,8)[0]==zlib.crc32(data[12:])
    return struct.unpack_from('<256I',data,12)


def stable_game(sftp,expected_boots,minimum_ticks,expected_parent=None,fd_baseline=None):
    deadline=time.monotonic()+35
    while True:
        guard()
        pid=parent()
        if core()=='TIC-80' and pid.isdigit():
            if expected_parent and pid!=expected_parent: raise RuntimeError('Supervisor changed during same-core reload')
            try: current=save(sftp)
            except FileNotFoundError: current=None
            if current:
                assert current[1]<=expected_boots, ('Duplicate BOOT',current[:3],expected_boots,log())
                if current[1]==expected_boots and current[0]>=minimum_ticks:
                    text=log()
                    assert all(word not in text for word in ('timed out','stalled','Cartridge failed',
                        'Cartridge rejected','Save error','TIC-80:','transport unavailable')),text
                    children=command('ps -eo pid,ppid,comm | '
                        f'awk \'$2 == {pid} && $3 == "tic80-vm" {{print $1}}\'').split()
                    assert len(children)==1 and children[0].isdigit(),children
                    assert command(f'cat /proc/{children[0]}/comm').strip()=='tic80-vm'
                    fd_profile=descriptors(command,pid,fd_baseline)
                    status=command(f'cat /proc/{pid}/status')
                    rss=int(re.search(r'^VmRSS:\s+(\d+) kB',status,re.M)[1])
                    return dict(parent=pid,worker=children[0],rss_kib=rss,ticks=current[0],boots=current[1],**fd_profile)
        if time.monotonic()>deadline: raise RuntimeError('Cartridge did not stabilize: '+log())
        time.sleep(.1)


def menu():
    selected=core()
    if selected=='MENU' and not parent(): return None
    assert selected=='TIC-80'
    load('/media/fat/menu.rbf','TIC-80')
    deadline=time.monotonic()+20; menu_at=None
    while True:
        guard(); selected=core()
        if selected=='MENU' and menu_at is None: menu_at=time.monotonic()
        if selected=='MENU' and not parent() and not command('pidof tic80-vm || true').strip():
            return time.monotonic()-menu_at
        if time.monotonic()>deadline: raise RuntimeError('Service/worker did not exit in MENU')
        time.sleep(.05)


try:
    guard(); hashes={}
    for name,local,remote in [('player','build/arm/tic80-live','/media/fat/games/TIC-80/TIC-80'),
                             ('rbf','build/fpga/output_files/TIC80.rbf','/media/fat/_Other/TIC80_20260930.rbf')]:
        hashes[name]=hashlib.sha256((ROOT/local).read_bytes()).hexdigest()
        assert command('sha256sum '+remote).split()[0]==hashes[name]
    hook='/media/fat/games/TIC-80/_handler.sh'
    daemon='/media/fat/MiSTer_Frontier/Master_Daemon.sh'
    hook_hash=command('sha256sum '+hook).split()[0]
    daemon_hash=command('sha256sum '+daemon).split()[0]
    assert hook_hash==hashlib.sha256((ROOT/'games/TIC-80/_handler.sh').read_bytes()).hexdigest()
    saveid='tic80-lifecycle-'+uuid.uuid4().hex
    savepath='/media/fat/saves/TIC-80/'+hashlib.md5(saveid.encode()).hexdigest()+'.pmem'
    code=(f'-- script: lua\n-- saveid: {saveid}\n'
        'function BOOT() pmem(1,pmem(1)+1) end\n'
        'function TIC() pmem(0,pmem(0)+1); pmem(2,math.floor(time())); '
        'cls(pmem(0)%16); print("Reload and save test",28,50,12) end\n').encode()
    payload=bytes([17,0,0,0,5,len(code)&255,len(code)>>8,0])+code
    (folder/'counter.tic').write_bytes(payload)
    with c.open_sftp() as sftp:
        try: sftp.stat(savepath)
        except FileNotFoundError: pass
        else: raise RuntimeError('Fresh diagnostic save already exists')
        remote='/tmp/tic80-mister-dev/lifecycle'
        command('mkdir -p '+remote)
        with sftp.open(remote+'/counter.tic','wb') as file: file.write(payload)
        with sftp.open(remote+'/counter.mgl','w') as file:
            file.write('<mistergamedescription>\n <rbf>_Other/TIC80_20260930</rbf>\n'
                f' <file delay="3" type="f" index="0" path="{remote}/counter.tic"/>\n'
                '</mistergamedescription>\n')
        menu(); boots=ticks=0
        for cycle in range(a.cycles):
            load(remote+'/counter.mgl','MENU')
            boots+=1
            state=stable_game(sftp,boots,ticks+60)
            records.append(dict(cycle=cycle,phase='cold-entry',**state))
            base_fds,base_rss=state['fds'],state['rss_kib']
            rss_profile=[]
            for reload in range(a.reloads):
                old=state
                load('/media/fat/_Other/TIC80_20260930.rbf','TIC-80')
                boots+=1
                state=stable_game(sftp,boots,old['ticks']+60,old['parent'],base_fds)
                assert state['worker']!=old['worker'],state
                records.append(dict(cycle=cycle,phase='reload',reload=reload,**state))
                (folder/'hardware-progress.json').write_text(json.dumps(records,indent=2)+'\n')
                assert state['fds']<=base_fds+1,state
                rss_profile.append(state['rss_kib'])
                # Allow allocator warmup, then require a plateau. The former
                # 2 MiB ceiling hid a reproducible 240 KiB leak per reload.
                # Sixteen reloads expose persistent growth in one supervisor.
                if len(rss_profile)>=8:
                    tail=rss_profile[4:]
                    assert max(tail)-min(tail)<=128,dict(cycle=cycle,rss_kib=rss_profile)
            elapsed=menu()
            text=log(); assert re.search(r'TIC-80 service stopped: ticks=\d+ error=0',text),text
            assert all(word not in text for word in ('timed out','stalled','transport unavailable')),text
            assert elapsed<.75,elapsed
            final=save(sftp); assert final[1]==boots and final[0]>=state['ticks'],final[:3]
            ticks=final[0]
            records.append(dict(cycle=cycle,phase='menu-exit',ticks=ticks,boots=boots,exit_after_menu_s=elapsed))
            (folder/f'cycle-{cycle}-exit.log').write_text(text)
            print(f'Cycle {cycle+1}: entry, reload and exit, BOOT={boots}, ticks={ticks}, clean MENU exit {elapsed:.3f}s',flush=True)
        assert command('sha256sum '+hook).split()[0]==hook_hash
        assert command('sha256sum '+daemon).split()[0]==daemon_hash
        result=dict(hashes=hashes,saveid=saveid,cycles=a.cycles,reloads_per_cycle=a.reloads,
                    memory_plateau_checked=a.reloads>=8,memory_plateau_limit_kib=128,
                    hook_sha256=hook_hash,daemon_sha256=daemon_hash,records=records,complete=True)
        (folder/'hardware-result.json').write_text(json.dumps(result,indent=2)+'\n')
        print('Native reload/MENU lifecycle stress passed',flush=True)
finally:
    c.close()
