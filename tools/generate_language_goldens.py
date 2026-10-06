"""Render the two native-demo poses used for live pipeline comparisons."""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--player", required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1] / "build"
output = root / "language-golden-rgba"
output.mkdir(parents=True, exist_ok=True)
for record in json.loads((root / "live-languages.json").read_text()):
    name = record["language"]
    for ticks in (30,60):
        subprocess.run([a.player, str(root / "languages" / (name + ".tic")), str(ticks),
                        str(output / f"{name}-{ticks}.rgba"),
                        str(output / f"{name}-{ticks}.s16le")], check=True)
