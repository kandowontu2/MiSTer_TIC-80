"""Compare real TIC-80 ticks against the independent moving-raster oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from motion_fixture import cartridge, inspect_rgb

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--player', type=Path, required=True)
args = parser.parse_args()
folder = ROOT / 'build/motion'
folder.mkdir(parents=True, exist_ok=True)
cart = folder / 'motion.tic'
cart.write_bytes(cartridge())
results = []
for ticks in (1, 238, 600):
    prefix = folder / f'desktop-{ticks - 1:03}'
    frame_path = prefix.with_suffix('.rgba')
    audio_path = prefix.with_suffix('.s16le')
    subprocess.run([str(args.player), str(cart), str(ticks), str(frame_path), str(audio_path)], check=True)
    rgba = frame_path.read_bytes()
    assert len(rgba) == 256 * 144 * 4
    rgb = bytes(rgba[index] for index in range(len(rgba)) if index % 4 != 3)
    frame = inspect_rgb(rgb)
    assert frame == ticks - 1
    results.append(dict(frame=frame, mismatching_channels=0, rgba_sha256=hashlib.sha256(rgba).hexdigest()))
result = dict(player_sha256=hashlib.sha256(args.player.read_bytes()).hexdigest(),
              cartridge_sha256=hashlib.sha256(cart.read_bytes()).hexdigest(), frames=results)
(folder / 'desktop-result.json').write_text(json.dumps(result, indent=2) + '\n')
print('Real TIC-80 motion ticks match the independent pixel oracle:', json.dumps(result))
