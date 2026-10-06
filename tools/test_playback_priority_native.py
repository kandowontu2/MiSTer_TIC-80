"""Check playback priority and audio continuity during ordinary SSH load."""
import hashlib
import json
import os
from pathlib import Path
import time
import uuid
import paramiko
from hardware_ssh import command, connect

root = Path(__file__).resolve().parents[1]
build = root/'build'
installed = json.loads((build/'playback-priority-installed.json').read_text())
client = paramiko.SSHClient()
client.load_system_host_keys()
connect(client,'192.168.1.176',username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)
prefix = 'priority-load-'+uuid.uuid4().hex[:8]
remote = '/tmp/tic80-mister-dev/'+prefix
result = dict(prefix=prefix,hashes=installed['hashes'],passed=False)
def selected():
    core = command(client,'cat /tmp/CORENAME').strip()
    assert core in ('MENU','TIC-80'), 'Another core selected'
    return core
def processes():
    raw = command(client,'for p in $(pidof TIC-80); do cat /proc/$p/stat; done; true')
    found = []
    for line in raw.splitlines():
        end = line.rfind(')'); fields = line[end+2:].split()
        found.append(dict(pid=int(line.split()[0]),comm=line[line.index('(')+1:end],
                          ppid=int(fields[1]),nice=int(fields[16])))
    return found
try:
    selected()
    for path,sha in installed['hashes'].items(): assert command(client,'sha256sum '+path).split()[0] == sha
    before = command(client,'cat /media/fat/logs/TIC-80/tic80.log')
    prior = [p['pid'] for p in processes() if p['comm']=='TIC-80']
    core = selected()
    command(client,'test "$(cat /tmp/CORENAME)" = '+core+' && '
            'printf "load_core /tmp/tic80-mister-dev/controller-keys/tetris.mgl\\n" > /dev/MiSTer_cmd')
    deadline = time.monotonic()+40
    marker = 'Cartridge loaded: 25147 bytes; reset=0'
    while True:
        selected(); current = processes(); log = command(client,'cat /media/fat/logs/TIC-80/tic80.log')
        parents = [p for p in current if p['comm']=='TIC-80']
        if parents and marker in log and (log.count(marker)>before.count(marker) or parents[0]['pid'] not in prior): break
        assert time.monotonic()<deadline, 'Tetris did not start'
        time.sleep(.25)
    time.sleep(2)
    assert selected()=='TIC-80'
    current = processes(); parents = [p for p in current if p['comm']=='TIC-80']; assert len(parents)==1
    parent = parents[0]
    workers = [p for p in current if p['comm']=='tic80-vm' and p['ppid']==parent['pid']]
    assert len(workers)==1 and parent['nice']==-10 and workers[0]['nice']==-10, current
    tasks = command(client,'for t in /proc/'+str(parent['pid'])+'/task/*; do cat "$t/stat"; done')
    thread_priorities = {}
    for line in tasks.splitlines():
        fields=line[line.rfind(')')+2:].split(); thread_priorities[int(line.split()[0])]=int(fields[16])
    assert len(thread_priorities)==2 and thread_priorities[parent['pid']]==-10
    assert all(nice==19 for tid,nice in thread_priorities.items() if tid!=parent['pid']),thread_priorities
    result.update(processes=current,threads=thread_priorities)
    # The wrapper records its terminal state; all output descriptors are detached
    # from SSH. Start it once and observe the same job throughout the transfer.
    handle = command(client,'test "$(cat /tmp/CORENAME)" = TIC-80 || exit 31; '
        '( /tmp/tic80-mister-dev/runtime-monitor --seconds 30 --interval-ms 20 > '+remote+'.jsonl 2> '+remote+'.err; '
        'printf "%s\\n" "$?" > '+remote+'.status ) </dev/null >/dev/null 2>&1 & printf "%s\\n" "$!"').strip()
    assert handle.isdigit(); result['monitor_handle']=int(handle)
    payload = build/'arm/studio_session_test'; sha=hashlib.sha256(payload.read_bytes()).hexdigest()
    result.update(payload_sha256=sha,payload_bytes=payload.stat().st_size,uploads=3,hash_iterations=5)
    with client.open_sftp() as sftp:
        for n in range(3):
            assert selected()=='TIC-80'
            target=remote+'-payload-'+str(n); sftp.put(str(payload),target)
            assert command(client,'sha256sum '+target).split()[0]==sha
    hashes = command(client,'test "$(cat /tmp/CORENAME)" = TIC-80 || exit 31; '
                     'for n in 1 2 3 4 5; do taskset 1 sha256sum '+remote+'-payload-2 || exit 32; done')
    assert len(hashes.splitlines())==5 and all(line.split()[0]==sha for line in hashes.splitlines())
    deadline=time.monotonic()+40
    while True:
        assert selected()=='TIC-80'
        state=command(client,'if test -f '+remote+'.status; then cat '+remote+'.status; else printf running; fi').strip()
        if state!='running': break
        assert time.monotonic()<deadline, 'Monitor observation expired; inspect its original handle'
        time.sleep(.25)
    result['monitor_status']=int(state)
    with client.open_sftp() as sftp:
        for suffix in ('.jsonl','.err'): sftp.get(remote+suffix,str(build/(prefix+suffix)))
    rows=[json.loads(line) for line in (build/(prefix+'.jsonl')).read_text().splitlines()]
    result.update(samples=len(rows),first=rows[0],last=rows[-1],underruns=max(row['underruns'] for row in rows))
    assert state=='0' and len(rows)>1000 and result['underruns']==0,result
    assert len({row['session'] for row in rows})==1
    for field in ('slots','written','played','publication','presented','heartbeat'):
        assert 0<((rows[-1][field]-rows[0][field])&0xffffffff)<0x80000000,field
    assert [p['pid'] for p in processes() if p['comm']=='TIC-80']==[parent['pid']]
    assert selected()=='TIC-80'; result['passed']=True
    print('Foreground nice -10, background save nice 19; three large transfers and checksums: zero underruns')
finally:
    (build/'playback-priority-native.json').write_text(json.dumps(result,indent=2)+'\n')
    client.close()
