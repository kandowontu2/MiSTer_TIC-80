"""Build the recovery utility against the production ARM session layout."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--sdk-build", type=Path, default=root / "build/mister-sdk")
a = p.parse_args()
sdk = a.sdk_build.resolve()
flags = (sdk / "CMakeFiles/tic80_studio_session.dir/flags.make").read_text()
cc = re.search(r"# compile C with (.*)", flags)[1]
args = [cc] + shlex.split(re.search(r"C_DEFINES = (.*)", flags)[1]) + shlex.split(re.search(r"C_INCLUDES = (.*)", flags)[1])
args += ["-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
         str(root / "tools/studio_checkpoint_export.c"), str(sdk / "runtime_adapters/cart.c"),
         "-Wl,--start-group", str(sdk / "libtic80core.a"), str(sdk / "libpng.a"), str(sdk / "libzlib.a"),
         str(sdk / "libgiflib.a"),
         "-Wl,--end-group", "-lm", "-o", str(a.output)]
subprocess.run(args, check=True)
