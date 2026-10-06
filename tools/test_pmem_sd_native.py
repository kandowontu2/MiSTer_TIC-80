"""Exercise the production save writer's fault contract on private MiSTer SD files."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import time
import uuid

import paramiko
from hardware_ssh import connect
from install_studio_candidate import Installer

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--trace-playback', action='store_true', help='Record playback counters around each I/O phase')
    args = parser.parse_args()
    installed = json.loads((BUILD / 'studio-installed-latest.json').read_text())
    plan_path = BUILD / installed['plan']
    plan = json.loads(plan_path.read_text())
    assert plan['installation_complete'] and plan['host'] == args.host
    pointer = json.loads((BUILD / 'native-pmem-sd-fixture-latest.json').read_text())
    fixture = BUILD / pointer['folder']
    assert sha(fixture / 'manifest.json') == pointer['manifest_sha256']
    manifest = json.loads((fixture / 'manifest.json').read_text())
    binary = fixture / 'pmem-fault-mister-sdk'
    assert manifest['host_passed'] and sha(binary) == manifest['variants']['mister-sdk']['binary_sha256']
    assert sha(ROOT / 'src/pmem.c') == manifest['pmem_source_sha256']
    assert json.loads((fixture / 'abi.json').read_text())['actual_board_imports_passed']
    prefix = 'pmem-sd-faults-' + uuid.uuid4().hex[:8]
    folder = BUILD / prefix
    folder.mkdir()
    (folder / 'driver.py').write_bytes(Path(__file__).read_bytes())
    remote = '/media/fat/games/TIC-80/.native-tests/' + prefix
    result = dict(started_at=datetime.now(timezone.utc).isoformat(), passed=False,
        prefix=prefix, fixture=pointer, scope=manifest['scope'], product_changed=False)
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    connect(client, args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
    installer = Installer(client, Path(plan['package']), plan_path, plan['stage'].split('/')[-1])
    run = installer.run
    profile = json.loads((BUILD / 'mgl-popup-native-latest.json').read_text())
    phases = []

    def trace(label):
        if not args.trace_playback:
            return
        assert installer.selected() == 'TIC-80'
        raw = run(profile['remote'] + '/runtime-monitor --seconds 1 --interval-ms 200')
        rows = [json.loads(line) for line in raw.splitlines()]
        assert len(rows) >= 2 and len({row['session'] for row in rows}) == 1
        phases.append(dict(label=label, recorded_at=datetime.now(timezone.utc).isoformat(), first=rows[0], last=rows[-1]))
        (folder / (label + '-samples.jsonl')).write_text(raw)
        (folder / 'playback-phases.json').write_text(json.dumps(phases, indent=2) + '\n')
        print(label + ': underrun slots=' + str(rows[-1]['underruns']), flush=True)

    try:
        selected = installer.selected()
        trace('before-guard')
        assert installer.snapshot(plan['after']) == plan['after']
        installer.running_main()
        trace('after-guard')
        filesystem = run("awk '$2 == \"/media/fat\" {print $3}' /proc/mounts").strip()
        assert filesystem, 'SD-card mount type not observed'
        run('set -e; test "$(cat /tmp/CORENAME)" = ' + selected + '; test ! -e ' + remote + '; mkdir -p ' + remote)
        with client.open_sftp() as sftp:
            sftp.put(str(binary), remote + '/fixture')
        trace('after-upload')
        assert run('sha256sum ' + remote + '/fixture').split()[0] == sha(binary)
        trace('after-upload-hash')
        assert not run('find ' + remote + ' -mindepth 1 -maxdepth 1 ! -name fixture -print').strip()
        run('chmod +x ' + remote + '/fixture')
        # Launch once and collect the original status/log. Observation never
        # restarts or duplicates the syscall/crash test after an SSH timeout.
        shell = ('TM_PMEM_FAULT_ROOT=' + shlex.quote(remote) + ' taskset 2 nice -n 19 ' + remote + '/fixture > ' + remote + '/test.log 2>&1; '
                 'printf "%s\\n" "$?" > ' + remote + '/test.status')
        pid = run('set -e; test "$(cat /tmp/CORENAME)" = ' + selected + '; nohup /bin/sh -c ' + shlex.quote(shell) + ' > ' + remote + '/wrapper.log 2>&1 < /dev/null & printf "%s\\n" "$!"').strip()
        assert pid.isdigit()
        result['original_wrapper_pid'] = int(pid)
        (folder / 'in-progress.json').write_text(json.dumps(result, indent=2) + '\n')
        trace('after-launch')
        deadline = time.monotonic() + 45
        while not run('test -f ' + remote + '/test.status && echo complete || true').strip():
            assert time.monotonic() < deadline, 'Original save-fault fixture still pending; do not restart it'
            assert run('kill -0 ' + pid + ' && echo alive').strip() == 'alive'
            time.sleep(.25)
        trace('after-fixture')
        with client.open_sftp() as sftp:
            for name in ('test.log', 'test.status', 'wrapper.log'):
                sftp.get(remote + '/' + name, str(folder / name))
        text = (folder / 'test.log').read_text()
        assert (folder / 'test.status').read_text().strip() == '0', text
        assert 'crash boundaries and final flush passed' in text
        remaining = run('find ' + remote + ' -mindepth 1 -maxdepth 1 -type d -print').strip()
        assert not remaining, remaining
        assert installer.snapshot(plan['after']) == plan['after']
        installer.running_main()
        trace('after-final-guard')
        result['writer_contract_passed'] = True
        result['playback_phases'] = phases
        if args.trace_playback:
            assert len({phase['last']['session'] for phase in phases}) == 1
            assert all(phase['first']['underruns'] == phase['last']['underruns'] == 0 for phase in phases), phases
        result.update(passed=True, private_test_directory=remote, filesystem=filesystem,
                      final_core=installer.selected(), test_directory_clean=True,
                      binary_sha256=sha(binary), log_sha256=sha(folder / 'test.log'))
        print('Native SD save-fault contract passed: atomic error handling, retry, blocked-write coalescing, SIGKILL boundaries and final flush; product files unchanged')
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        (BUILD / 'pmem-sd-native-latest.json').write_text(json.dumps(dict(folder=prefix, **result), indent=2) + '\n')
        client.close()


if __name__ == '__main__':
    main()
