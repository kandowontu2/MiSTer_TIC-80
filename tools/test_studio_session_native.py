"""Measure the standalone Studio supervisor on an otherwise idle playback CPU."""
import argparse
import hashlib
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
parser.add_argument('--nice',choices=('-10','19'),default='-10')
parser.add_argument('--profile',action='store_true',help='Use the diagnostic phase-profiler fixture and separate results')
args=parser.parse_args()
client=paramiko.SSHClient(); client.load_system_host_keys()
connect(client,'192.168.1.176',username='root',password=os.environ['TM_SSH_PASSWORD'],timeout=10)
installed=json.loads((build/'video-clock-installed.json').read_text())
prefix='studio-native-'+uuid.uuid4().hex[:8]
remote='/tmp/tic80-mister-dev/'+prefix
fixture=build/'arm'/('studio_session_profile_test' if args.profile else 'studio_session_test')
result=dict(prefix=prefix,installed=installed,passed=False)
initial=None
def selected():
    core=command(client,'cat /tmp/CORENAME').strip()
    assert core in ('TIC-80','MENU'),'Another core selected'
    return core
def supervisor():
    return command(client,'for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()
try:
    initial=selected()
    for path,sha in installed.items(): assert command(client,'sha256sum '+path).split()[0]==sha
    cart='/media/fat/games/TIC-80/Carts/tetris.tic'
    cart_sha=command(client,'sha256sum '+cart).split()[0]
    assert cart_sha=='b2090c8279cec7e7a31adecbdf44498ac4d02da8fb3b0f11e021305983b8bdbe'
    equality=build/'arm/memory_equal_test'
    with client.open_sftp() as sftp:
        sftp.put(str(fixture),remote)
        if args.profile: sftp.put(str(equality),remote+'-equality')
    sha=hashlib.sha256(fixture.read_bytes()).hexdigest()
    assert command(client,'sha256sum '+remote).split()[0]==sha
    current=selected(); command(client,'test "$(cat /tmp/CORENAME)" = '+current+' && chmod +x '+remote)
    if current=='TIC-80':
        command(client,'test "$(cat /tmp/CORENAME)" = TIC-80 && printf "load_core /media/fat/menu.rbf\\n" > /dev/MiSTer_cmd')
    deadline=time.monotonic()+20
    while selected()!='MENU' or supervisor():
        assert time.monotonic()<deadline,'Own service did not stop'
        time.sleep(.2)
    if args.profile:
        equality_sha=hashlib.sha256(equality.read_bytes()).hexdigest()
        assert command(client,'sha256sum '+remote+'-equality').split()[0]==equality_sha
        command(client,'test "$(cat /tmp/CORENAME)" = MENU && chmod +x '+remote+'-equality')
        equal_output=command(client,'test "$(cat /tmp/CORENAME)" = MENU && timeout 15 taskset 1 '+remote+'-equality 2>&1',timeout=25)
        assert 'tails and guard pages passed' in equal_output
        (build/(prefix+'-equality.log')).write_text(equal_output)
        result['memory_equality']=dict(passed=True,fixture_sha256=equality_sha)
    output=command(client,'test "$(cat /tmp/CORENAME)" = MENU && timeout 45 nice -n '+args.nice+' taskset 1 '+remote+' --benchmark-cart '+cart+' 2>&1',timeout=55)
    (build/(prefix+'.log')).write_text(output)
    assert 'child cleanup passed' in output
    match=re.search(r'cart_size=(\d+), mean=([\d.]+) ms p50=([\d.]+) ms p95=([\d.]+) ms p99=([\d.]+) ms max=([\d.]+) ms over_60hz_budget=(\d+)',output)
    assert match,output
    values=match.groups()
    result.update(fixture_sha256=sha,cart_sha256=cart_sha,cart_size=int(values[0]),
        timing_ms=dict(zip(('mean','p50','p95','p99','max'),map(float,values[1:6]))),over_budget=int(values[6]),
        nice=int(args.nice),scope='Standalone supervisor and real Tetris on CPU 0 with Main on MENU; excludes FPGA transport/render conversion')
    assert selected()=='MENU'; result['passed']=True
    if args.profile:
        phases=re.findall(r'Studio profile: mode=(\d+) ticks=(\d+) tick_ms=([\d.]+) sound_ms=([\d.]+) publish_ms=([\d.]+)',output)
        assert phases,'Profile fixture emitted no phase measurements'
        result['phases']=[dict(mode=int(p[0]),ticks=int(p[1]),tick_ms=float(p[2]),sound_ms=float(p[3]),publish_ms=float(p[4])) for p in phases]
        checkpoints=re.findall(r'Studio checkpoint profile: publications=(\d+) dirty_pages=(\d+) hashes=(\d+) scan_ms=([\d.]+) hash_ms=([\d.]+) output_ms=([\d.]+)',output)
        assert checkpoints,'Profile fixture emitted no checkpoint measurements'
        result['checkpoints']=[dict(publications=int(p[0]),dirty_pages=int(p[1]),hashes=int(p[2]),scan_ms=float(p[3]),hash_ms=float(p[4]),output_ms=float(p[5])) for p in checkpoints]
    print(json.dumps(result,indent=2))
finally:
    try:
        if initial=='TIC-80' and selected()=='MENU':
            command(client,'test "$(cat /tmp/CORENAME)" = MENU && '
                    'printf "load_core /tmp/tic80-mister-dev/controller-keys/tetris.mgl\\n" > /dev/MiSTer_cmd')
            deadline=time.monotonic()+40
            while selected()!='TIC-80' or not supervisor():
                assert time.monotonic()<deadline,'Tetris restoration did not start'
                time.sleep(.25)
            time.sleep(2); assert selected()=='TIC-80'
            raw=command(client,'/tmp/tic80-mister-dev/runtime-monitor --seconds 1 --interval-ms 200')
            (build/(prefix+'-restored.jsonl')).write_text(raw)
            samples=[json.loads(line) for line in raw.splitlines()]
            result['restored']=dict(first=samples[0],last=samples[-1])
            assert max(row['underruns'] for row in samples)==0
            result['restoration_passed']=True
    finally:
        name='studio-supervision-profile-native.json' if args.profile else 'studio-supervision-native.json'
        (build/name).write_text(json.dumps(result,indent=2)+'\n')
        (build/(prefix+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        client.close()
