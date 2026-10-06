"""Fetch the exact upstream runtime and its pinned dependencies."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference" / "tic80"
REVISION = "4dba5bc2640d9cde650fb0b427c9be6aab598de9"
DEPENDENCIES = ["blip-buf", "lua", "zlib", "libpng", "giflib", "dirent", "dlfcn",
                "quickjs", "pocketpy", "wren", "squirrel", "moonscript", "lpeg",
                "yuescript", "janet", "wasm3", "mruby", "pforth", "miniscript2",
                "zip", "argparse", "jsmn"]

def run(*args):
    subprocess.run(args, check=True)

if not SOURCE.exists():
    run("git", "clone", "https://github.com/nesbox/TIC-80.git", str(SOURCE))
head = subprocess.check_output(["git", "-C", str(SOURCE), "rev-parse", "HEAD"], text=True).strip()
if head != REVISION:
    dirty = subprocess.check_output(["git", "-C", str(SOURCE), "status", "--porcelain"], text=True)
    if dirty:
        raise SystemExit("TIC-80 checkout has changes; refusing to change its revision")
    run("git", "-C", str(SOURCE), "fetch", "origin", REVISION)
    run("git", "-C", str(SOURCE), "checkout", "--detach", REVISION)
run("git", "-C", str(SOURCE), "submodule", "update", "--init", "--depth", "1",
    *("vendor/" + name for name in DEPENDENCIES))
