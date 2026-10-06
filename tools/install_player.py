"""Install a locally checked ARM player against a captured hardware baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import time
import paramiko
from hardware_access import require_access
from hardware_ssh import command, connect

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tag', required=True)
args = parser.parse_args()
assert re.fullmatch('[a-z0-9]+(?:-[a-z0-9]+)*', args.tag)
tag = args.tag
require_access('192.168.1.176')
build = root/'build'
baseline = json.loads((build/(tag+'-baseline')/'video-clock-installed.json').read_text())
player = build/'arm/tic80-live'
payload = player.read_bytes()
assert payload[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(payload[18:20], 'little') == 40
tests = build/(tag+'-host-tests.log')
assert '100% tests passed' in tests.read_text()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
sha = digest(player)
sources = {str(path.relative_to(root)).replace('\\','/'): digest(path)
           for directory in ('src', 'include', 'cmake') for path in (root/directory).rglob('*') if path.is_file()}
sources['CMakeLists.txt'] = digest(root/'CMakeLists.txt')
remote = '/media/fat/games/TIC-80/TIC-80'
client = paramiko.SSHClient()
client.load_system_host_keys()
connect(client,'192.168.1.176', username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
def selected():
    core = command(client,'cat /tmp/CORENAME').strip()
    assert core in ('MENU', 'TIC-80'), 'Another core selected'
    return core
def supervisor():
    return command(client,'for p in $(pidof TIC-80); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80 && '
                   'printf "%s " "$p"; done; true').strip()
try:
    selected()
    for path, expected in baseline.items(): assert command(client,'sha256sum '+path).split()[0] == expected, path
    assert digest(build/(tag+'-baseline')/'TIC-80') == baseline[remote]
    if selected() == 'TIC-80':
        command(client,'test "$(cat /tmp/CORENAME)" = TIC-80 && printf "load_core /media/fat/menu.rbf\\n" > /dev/MiSTer_cmd')
    deadline = time.monotonic()+20
    while selected() != 'MENU' or supervisor():
        assert time.monotonic() < deadline, 'Own service did not stop'
        time.sleep(.2)
    rollback = '/media/fat/games/TIC-80/.rollback-'+tag+'/TIC-80.'+baseline[remote][:12]
    command(client,'test "$(cat /tmp/CORENAME)" = MENU && mkdir -p '+str(Path(rollback).parent).replace('\\','/'))
    command(client,'test "$(cat /tmp/CORENAME)" = MENU && cp -n '+remote+' '+rollback+' && sync')
    assert command(client,'sha256sum '+rollback).split()[0] == baseline[remote]
    stage = '/tmp/tic80-mister-dev/'+tag+'-player'
    with client.open_sftp() as sftp: sftp.put(str(player),stage)
    assert digest(player) == sha and command(client,'sha256sum '+stage).split()[0] == sha
    command(client,'set -e; test "$(cat /tmp/CORENAME)" = MENU; cp '+stage+' '+remote+'.new; chmod +x '+remote+'.new; sync')
    assert command(client,'sha256sum '+remote+'.new').split()[0] == sha
    command(client,'test "$(cat /tmp/CORENAME)" = MENU && mv '+remote+'.new '+remote+' && sync')
    assert command(client,'sha256sum '+remote).split()[0] == sha
    installed = dict(baseline); installed[remote] = sha
    for name in ('video-clock-installed.json','lifecycle-installed.json','audio-pacing-installed.json'):
        (build/name).write_text(json.dumps(installed,indent=2)+'\n')
    result = dict(hashes=installed,previous=baseline[remote],rollback=rollback,sources=sources,
                  tests_sha256=digest(tests),qualification='Local checks passed; native playback checks pending')
    (build/(tag+'-installed.json')).write_text(json.dumps(result,indent=2)+'\n')
    print('ARM player installed with verified rollback:',sha)
finally:
    client.close()
