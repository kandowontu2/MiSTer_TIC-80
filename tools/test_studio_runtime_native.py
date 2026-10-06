"""Compare all pinned runtime demos through the installed Studio MGL path."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import time
import uuid

import paramiko
from PIL import Image
from hardware_ssh import command, connect
from hardware_source import source_snapshot
from install_studio_candidate import Installer

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
LANGUAGES = 'lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth'.split()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    args = parser.parse_args()
    installed = json.loads((BUILD / 'studio-installed-latest.json').read_text())
    assert installed['installation_complete'] and installed['frontend'] == 'studio'
    plan_path = BUILD / installed['plan']
    plan = json.loads(plan_path.read_text())
    assert plan['installation_complete'] and plan['host'] == args.host
    pointer = json.loads((BUILD / 'studio-runtime-fixtures-latest.json').read_text())
    fixture = BUILD / pointer['folder']
    assert sha(fixture / 'manifest.json') == pointer['manifest_sha256']
    manifest = json.loads((fixture / 'manifest.json').read_text())
    assert manifest['desktop_completed'] and manifest['languages'] == LANGUAGES
    for name, record in manifest['files'].items():
        assert sha(fixture / name) == record['sha256'], name
    reference_pointer = json.loads((BUILD / 'studio-runtime-reference-latest.json').read_text())
    reference = BUILD / reference_pointer['folder']
    assert sha(reference / 'manifest.json') == reference_pointer['manifest_sha256']
    reference_manifest = json.loads((reference / 'manifest.json').read_text())
    assert reference_manifest['complete'] and reference_manifest['fixture'] == pointer
    for name, record in reference_manifest['files'].items():
        assert sha(reference / name) == record['sha256'], name
    expected_mouse = reference_manifest['mouse']['raw_word']
    candidate = json.loads((BUILD / 'studio-playback-candidate/manifest.json').read_text())
    for name, digest in candidate['compiled_sources'].items():
        assert sha(ROOT / name) == digest, name
    profile = json.loads((BUILD / 'mgl-popup-native-latest.json').read_text())
    prefix = 'studio-runtime-' + uuid.uuid4().hex[:8]
    folder = BUILD / prefix
    folder.mkdir()
    (folder / 'driver.py').write_bytes(Path(__file__).read_bytes())
    remote = '/media/fat/games/TIC-80/.native-tests/' + prefix
    result = dict(started_at=datetime.now(timezone.utc).isoformat(), passed=False,
        prefix=prefix, fixture=pointer, reference=reference_pointer,
        installed_state_sha256=sha(BUILD / 'studio-installed-latest.json'),
        payload=installed['payload'], cases=[], restoration_requested=False)
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    connect(client, args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
    installer = Installer(client, Path(plan['package']), plan_path, plan['stage'].split('/')[-1])
    run = installer.run
    monitor = profile['remote'] + '/runtime-monitor'
    log_path = '/media/fat/logs/TIC-80/tic80.log'

    def persist():
        (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        (BUILD / 'studio-runtime-native-latest.json').write_text(json.dumps(dict(folder=prefix, **result), indent=2) + '\n')

    def guard():
        installer.selected()
        assert installer.snapshot(plan['after']) == plan['after']
        return installer.running_main()

    def load(path):
        selected = installer.selected()
        run('test "$(cat /tmp/CORENAME)" = ' + selected + ' && printf ' +
            shlex.quote('load_core ' + path + '\n') + ' > /dev/MiSTer_cmd')

    def log():
        return run('cat ' + log_path + ' 2>/dev/null || true')

    def parent():
        pids = run('for p in $(pidof TIC-80-Studio); do test "$(cat /proc/$p/comm 2>/dev/null)" = TIC-80-Studio && printf "%s " "$p"; done; true').split()
        assert len(pids) <= 1, pids
        return pids[0] if pids else None

    def mgl(path, index):
        return ('<mistergamedescription>\n <rbf>_Other/TIC80_20261003</rbf>\n'
                f' <file delay="3" type="f" index="{index}" path="{path}"/>\n'
                '</mistergamedescription>\n')

    try:
        guard()
        assert run('sha256sum ' + monitor).split()[0] == profile['candidates']['runtime-monitor']
        selected = installer.selected()
        run('set -e; test "$(cat /tmp/CORENAME)" = ' + selected + '; test ! -e ' + remote + '; mkdir -p ' + remote + '/carts')
        with client.open_sftp() as sftp:
            for path in sorted((fixture / 'carts').iterdir()):
                sftp.put(str(path), remote + '/carts/' + path.name)
                assert run('sha256sum ' + remote + '/carts/' + path.name).split()[0] == sha(path)
            for format_, suffix, index in (('native', '.tic', 0), ('png', '.png', 64), ('legacy', '-legacy.png', 64)):
                for language in LANGUAGES:
                    label = format_ + '-' + language
                    path = remote + '/carts/' + language + suffix
                    cart = fixture / 'carts' / (language + suffix)
                    remote_mgl = remote + '/' + label + '.mgl'
                    data = mgl(path, index)
                    (folder / (label + '.mgl')).write_text(data)
                    with sftp.open(remote_mgl, 'wb') as stream:
                        stream.write(data.encode())
                    guard()
                    previous_log = log()
                    previous_pid = parent()
                    load(remote_mgl)
                    deadline = time.monotonic() + 40
                    while True:
                        installer.selected()
                        text = log()
                        pid = parent()
                        fresh = text.count('MiSTer cartridge running:') > previous_log.count('MiSTer cartridge running:') or (pid and pid != previous_pid)
                        if pid and fresh and 'MiSTer cartridge running:' in text and ('MiSTer cartridge selected: ' + path) in text:
                            break
                        assert time.monotonic() < deadline, (label, text)
                        time.sleep(.2)
                    assert run('sha256sum /proc/' + pid + '/exe').split()[0] == installed['payload']['games/TIC-80/TIC-80-Studio']['sha256']
                    source = source_snapshot(run, path, cart.stat().st_size)
                    samples = run(monitor + ' --seconds 2 --interval-ms 13')
                    (folder / (label + '-audio.jsonl')).write_text(samples)
                    rows = [json.loads(line) for line in samples.splitlines()]
                    assert len(rows) >= 100 and len({row['session'] for row in rows}) == 1
                    assert all(row['underruns'] == 0 for row in rows)
                    assert ((rows[-1]['played'] - rows[0]['played']) & 0xffffffff) > 90000
                    screenshots = '/media/fat/screenshots/TIC-80'
                    mouse_before = int(run('devmem 0x3A000068 32').strip(), 16)
                    assert mouse_before == expected_mouse, 'Mouse input moved; reference requires a released, stationary pointer'
                    before = set(sftp.listdir(screenshots))
                    run('test "$(cat /tmp/CORENAME)" = TIC-80 && printf "screenshot\\n" > /dev/MiSTer_cmd')
                    deadline = time.monotonic() + 12
                    capture = folder / (label + '.png')
                    while True:
                        installer.selected()
                        names = sorted(name for name in set(sftp.listdir(screenshots)) - before if name.endswith('.png'))
                        if names:
                            sftp.get(screenshots + '/' + names[-1], str(capture))
                            if capture.read_bytes().endswith(b'\x00\x00\x00\x00IEND\xaeB\x60\x82'):
                                break
                        assert time.monotonic() < deadline, 'Screenshot incomplete: ' + label
                        time.sleep(.2)
                    with Image.open(capture) as image:
                        assert image.size == (256, 144)
                        actual = image.convert('RGB').tobytes()
                    mouse_after = int(run('devmem 0x3A000068 32').strip(), 16)
                    assert mouse_after == expected_mouse
                    mismatches = []
                    for ticks in (30, 60):
                        rgba = (reference / f'{language}-{ticks}.rgba').read_bytes()
                        expected = bytes(channel for pixel in zip(rgba[0::4], rgba[1::4], rgba[2::4]) for channel in pixel)
                        mismatches.append(sum(a != b for a, b in zip(actual, expected)))
                    case = dict(language=language, format=format_, cart_sha256=sha(cart),
                        source=source, parent=pid, samples=len(rows), first=rows[0], last=rows[-1],
                        mismatching_rgb_channels=min(mismatches), capture_sha256=sha(capture),
                        stationary_mouse_word=mouse_before)
                    result['cases'].append(case)
                    (folder / (label + '.log')).write_text(log())
                    persist()
                    assert case['mismatching_rgb_channels'] == 0, (label, mismatches)
                    guard()
                    print(label + ': exact desktop RGB, source ACK and zero measured underruns', flush=True)
        assert len(result['cases']) == 42
        result['passed'] = True
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        try:
            guard()
            # Leave the user's original Tetris cartridge in installed Studio.
            restored = remote + '/restore-tetris.mgl'
            with client.open_sftp() as sftp:
                with sftp.open(restored, 'wb') as stream:
                    stream.write(mgl('/media/fat/games/TIC-80/Carts/tetris.tic', 0).encode())
            load(restored)
            result['restoration_requested'] = True
        except BaseException as error:
            result['restoration_error'] = repr(error)
        persist()
        client.close()


if __name__ == '__main__':
    main()
