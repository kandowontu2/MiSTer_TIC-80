"""Run upstream cartridges through the actual player and check meaningful output."""
import argparse
from pathlib import Path
import subprocess
import tempfile
parser = argparse.ArgumentParser()
parser.add_argument("--player", required=True)
parser.add_argument("--converter", required=True)
parser.add_argument("--source", required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="tic80-demos-") as directory:
    work = Path(directory)
    for name, buttons in [("tetris", 16), ("sfx", 1), ("music", 16)]:
        cart, video, audio = (work / (name + suffix) for suffix in [".tic", ".rgba", ".s16le"])
        subprocess.run([args.converter, str(Path(args.source) / "demos" / (name + ".lua")), str(cart)], check=True)
        subprocess.run([args.player, str(cart), "600", str(video), str(audio), str(buttons)], check=True)
        frame = video.read_bytes()
        samples = audio.read_bytes()
        colors = {frame[i:i + 3] for i in range(0, len(frame), 4)}
        # The SFX demo deliberately uses only background and foreground colors.
        minimum_colors = 2 if name == "sfx" else 3
        if len(frame) != 256 * 144 * 4 or len(colors) < minimum_colors:
            raise SystemExit(f"{name}: missing or degenerate video")
        # This upstream Tetris project explicitly replaces sfx() with an empty
        # placeholder. Only the sound/music demos should produce audible PCM.
        if len(samples) != 600 * 800 * 4 or (name != "tetris" and not any(samples)):
            raise SystemExit(f"{name}: missing or silent audio")
        print(f"{name}: 600 ticks, video and PCM verified")
