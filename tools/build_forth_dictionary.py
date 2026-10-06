"""Generate a pForth dictionary using the target's cell size and endianness.

An ARM build runs its statically linked bootstrap with qemu-arm. Sources and
outputs stay separate from the pinned checkout and from other build trees.
"""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--source", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--compiler", required=True)
p.add_argument("--flags", default="")
p.add_argument("--runner")
a = p.parse_args()
work = a.output.parent.resolve()
work.mkdir(parents=True, exist_ok=True)
fth = work / "fth"
shutil.copytree(a.source / "fth", fth, dirs_exist_ok=True)
csrc = a.source / "csrc"
sources = [csrc / name.strip() for name in (csrc / "sources.cmake").read_text().splitlines()
           if name.strip().endswith(".c")]
sources += [csrc / "stdio" / name for name in ["pf_fileio_stdio.c", "pf_io_stdio.c"]]
exe = work / "pforth-bootstrap"
subprocess.run([a.compiler, *shlex.split(a.flags), "-O2", "-w", "-DPF_SUPPORT_FP",
                "-I" + str(csrc), *map(str, sources), str(csrc / "pf_main.c"),
                "-lm", "-o", str(exe)], check=True)
command = ([a.runner] if a.runner else []) + [str(exe)]
with open(work / "bootstrap.log", "w") as log, open("/dev/null", "rb") as empty:
    subprocess.run([*command, "-i", "system.fth"], cwd=fth, stdin=empty,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    subprocess.run([*command, "mkdicdat.fth"], cwd=fth, stdin=empty,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
generated = fth / "pfdicdat.h"
if not generated.is_file() or generated.stat().st_size < 1024:
    raise SystemExit("pForth dictionary generation produced no usable header")
shutil.copyfile(generated, a.output)
print("Generated target pForth dictionary:", a.output)
