"""Compare live pipeline PNGs with the demos' two animation poses.

Generate no-input golden frames at 30 and 60 ticks using tic80-player, named
build/language-golden-rgba/<language>-<ticks>.rgba. Compares full eight-bit channels.
"""
import json
import argparse
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1] / "build"
p = argparse.ArgumentParser()
p.add_argument("--format", choices=("native", "png", "legacy"), default="native")
p.add_argument("--label", default="")
p.add_argument("--languages", nargs='+',
               choices="lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth".split(),
               default="lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth".split())
a = p.parse_args()
if a.label and (len(a.label)>32 or any(ch not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for ch in a.label)):
    p.error('Invalid label')
suffix = ("-" + a.label if a.label else "") + ("" if a.format == "native" else "-" + a.format)
records = json.loads((root / f"live-languages{suffix}.json").read_text())
assert len(set(a.languages)) == len(a.languages)
assert [record['language'] for record in records] == a.languages, 'Runtime coverage does not match requested scope'
results = []
for record in records:
    name = record["language"]
    actual = Image.open(root / record["capture"]).convert("RGB").tobytes()
    best = len(actual)
    for ticks in (30, 60):
        raw = (root / "language-golden-rgba" / f"{name}-{ticks}.rgba").read_bytes()
        assert len(raw) == 256 * 144 * 4
        rgb = bytearray()
        for offset in range(0, len(raw), 4):
            rgb.extend(raw[offset:offset+3])
        assert len(actual) == len(rgb)
        best = min(best, sum(a != b for a,b in zip(actual,rgb)))
    results.append({"language": name, "mismatching_channels": best})
    print(name, "matching channels" if not best else f"MISMATCH: {best} channels")
(root / f"language-capture-parity{suffix}.json").write_text(json.dumps(results,indent=2) + "\n")
assert all(record["mismatching_channels"] == 0 for record in results)
