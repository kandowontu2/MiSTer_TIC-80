"""Collect pinned upstream notices for redistribution, with an exact inventory."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "reference/tic80"


def collect():
    out = ROOT / "licenses"
    out.mkdir(exist_ok=True)
    sources = [(ROOT / "LICENSE", "integration/LICENSE"), (ROOT / "NOTICE", "integration/NOTICE"),
               (UPSTREAM / "LICENSE", "TIC-80/LICENSE"),
               (ROOT / "reference/pico8/LICENSE", "MiSTer_PICO-8/LICENSE"),
               (ROOT / "vendor/MiSTer_Frontier/LICENSE", "MiSTer_Frontier/LICENSE"),
               (ROOT / "assets/LICENSE.cacert", "Mozilla-CA/LICENSE")]
    components = ["lua", "moonscript", "yuescript", "squirrel", "quickjs", "pocketpy", "wren",
                  "janet", "wasm3", "mruby", "miniscript2", "pforth", "blip-buf", "libpng",
                  "zlib", "giflib", "zip", "argparse", "jsmn", "dirent", "dlfcn"]
    for component in components:
        directory = UPSTREAM / "vendor" / component
        notices = [p for p in directory.rglob("*") if p.is_file() and
                   (p.name.lower().startswith(("license", "copying", "copyright"))) and ".git" not in p.parts]
        sources += [(p, component + "/" + p.relative_to(directory).as_posix()) for p in notices]
    # These upstreams carry their notices inside the source, not a standalone
    # license file. Preserve the full file; avoid paraphrasing legal notices.
    for path in ("vendor/lua/lua.h", "vendor/fennel/fennel.lua", "vendor/s7/s7.c",
                 "vendor/lpeg/lpeg.html", "src/ext/miniaudio.h", "src/ext/md5.c",
                 "vendor/blip-buf/blip_buf.c", "vendor/zip/src/miniz.h"):
        sources.append((UPSTREAM / path, "embedded-notices/" + path))
    inventory = []
    for source, name in sources:
        dest = out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        inventory.append({"file": name, "source": source.relative_to(ROOT).as_posix(),
                          "sha256": hashlib.sha256(dest.read_bytes()).hexdigest()})
    gcc = out / "GCC/COPYING.RUNTIME"
    if gcc.exists():
        inventory.append({"file": "GCC/COPYING.RUNTIME", "source": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-10.2.0/COPYING.RUNTIME",
                          "sha256": hashlib.sha256(gcc.read_bytes()).hexdigest()})
    (out / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    print(f"Collected {len(inventory)} original notice files")


if __name__ == "__main__":
    collect()
