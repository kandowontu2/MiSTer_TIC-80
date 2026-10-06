"""Package the frozen, qualified development binaries into an SD-card layout."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
from candidate_components import DESTINATIONS, qualified_hashes

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(destination, receipt_path):
    receipt = json.loads(receipt_path.read_text())
    assert receipt['bounded_studio_playback_checks_passed']
    assert receipt['fresh_hdmi_stereo_confirmation'] and receipt['restored_tetris_verified']
    assert not destination.exists(), 'Choose a new directory; preserve existing packages'
    qualified = qualified_hashes(receipt)
    specifications = [
        ('build/main-mgl-popup-20261003/source/bin/MiSTer', 'MiSTer', receipt['main_sha256'], True),
        ('build/fpga/output_files/TIC80.rbf', '_Other/TIC80_20261003.rbf', receipt['fpga_sha256'], False),
        ('build/studio-playback-candidate/tic80-studio-live', 'games/TIC-80/TIC-80-Studio', receipt['studio_sha256'], True),
        ('build/sdk-native-candidate/bin/tic80-live', 'games/TIC-80/TIC-80',
         qualified['games/TIC-80/TIC-80'], True),
        ('games/TIC-80/_handler.sh', 'games/TIC-80/_handler.sh', receipt['handler_sha256'], True),
        ('assets/cacert.pem', 'games/TIC-80/cacert.pem', qualified['games/TIC-80/cacert.pem'], False),
    ]
    if 'payload' in receipt:
        assert set(receipt['payload']) == DESTINATIONS, 'Incomplete qualified payload mapping'
        assert {'player_sha256', 'cacert_sha256'} <= receipt.keys(), 'Mapped payloads require explicit player and CA hashes'
        mapped = []
        for _, name, _, executable in specifications:
            item = receipt['payload'][name]
            source = item['source']
            assert isinstance(source, str) and source and '\\' not in source and ':' not in source
            path = PurePosixPath(source)
            assert not path.is_absolute() and '..' not in path.parts, 'Invalid source path: ' + source
            assert item['sha256'] == qualified[name], 'Unqualified mapped component: ' + name
            assert item['executable'] is executable, 'Incorrect executable role: ' + name
            mapped.append((source, name, qualified[name], executable))
        specifications = mapped
    # Check every source and the immutable evidence archive before creating any
    # output. The manifest binds the payload to this exact qualification receipt.
    for source, _, expected, _ in specifications:
        assert sha(ROOT / source) == expected, source
    evidence = receipt_path.parent / receipt['evidence_directory']
    for name, item in receipt['files'].items():
        assert sha(evidence / name) == item['sha256'], name
    destination.mkdir(parents=True)
    payload = destination / 'sd-card'
    files = {}
    for source, name, _, executable in specifications:
        target = payload / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / source, target)
        assert sha(target) == sha(ROOT / source)
        files[name] = dict(sha256=sha(target), bytes=target.stat().st_size,
                          executable=executable, source=source)
    notices = destination / 'licenses'
    notices.mkdir()
    for name in ('LICENSE', 'NOTICE', 'assets/LICENSE.cacert'):
        shutil.copyfile(ROOT / name, notices / Path(name).name)
    for source in (ROOT / 'build/sdk-native-candidate/licenses').iterdir():
        if source.is_file():
            shutil.copyfile(source, notices / source.name)
    shutil.copyfile(receipt_path, destination / 'qualification.json')
    manifest = dict(created_at=datetime.now(timezone.utc).isoformat(),
        kind='development-candidate', release_accepted=False, goal_complete=False,
        qualification_sha256=sha(receipt_path), files=files,
        requires='Existing Frontier Master Daemon, MiSTer ARM Linux ABI used in the qualification, and the included companion Main',
        mode='HDMI 720p/60, video_mode=0, vsync_adjust=0',
        open_gates=receipt['open_gates'])
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (destination / 'README.txt').write_text(
        'TIC-80 MiSTer development candidate\n\n'
        'The sd-card directory contains the qualified companion Main, FPGA core,\n'
        'Studio, cartridge player, launcher and CA bundle. Studio is the default.\n'
        'The existing Frontier Master Daemon launches games/TIC-80/_handler.sh.\n'
        'An optional games/TIC-80/frontend.txt containing player selects the player.\n'
        'Cartridges belong in games/TIC-80/Carts; saves and logs are preserved.\n\n'
        'Use MENU and stop the TIC-80 frontend before replacing files. Back up\n'
        'each existing destination first, preserve its hash and retain absent-file\n'
        'information for rollback. Apply executable permissions from manifest.json.\n'
        'The included Main and RBF are a matched pair: install both. On the\n'
        'installer requires this Main to be running. After changing Main,\n'
        'restart it from the loaded MENU or reboot before loading TIC-80.\n'
        'Use the qualified [TIC-80] video_mode=0 and vsync_adjust=0 settings.\n\n'
        'qualification.json describes bounded checks and links their hashed local\n'
        'evidence. This package does not establish all-cartridge compatibility,\n'
        'CRT support or physical microphone coverage. Physical input coverage\n'
        'and other remaining limitations are listed in qualification.json.\n'
    )
    print('Packaged', len(files), 'verified payload files:', destination)
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--receipt', type=Path, default=ROOT / 'build/studio-playback-qualification.json')
    args = parser.parse_args()
    package(args.destination.resolve(), args.receipt.resolve())
