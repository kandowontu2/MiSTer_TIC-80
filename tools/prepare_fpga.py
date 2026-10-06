"""Stage the pinned MiSTer platform with the explicit TIC-80 file overrides."""
from pathlib import Path
import argparse
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "reference/pico8"
EXPECTED = "72cb0405417d506c33e59ab51c1a374fc4db649e"
if not REFERENCE.exists():
    subprocess.run(["git", "clone", "https://github.com/MiSTerOrganize/MiSTer_PICO-8.git", str(REFERENCE)], check=True)
    subprocess.run(["git", "-C", str(REFERENCE), "checkout", "--detach", EXPECTED], check=True)
revision = subprocess.check_output(["git", "-C", str(REFERENCE), "rev-parse", "HEAD"], text=True).strip()
if revision != EXPECTED:
    raise SystemExit("PICO-8 reference revision differs; use the pinned revision from README.md")
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT / 'build/fpga')
STAGE = parser.parse_args().output.resolve()
if STAGE == REFERENCE.resolve() or REFERENCE.resolve() in STAGE.parents:
    raise SystemExit('Output must be separate from the pinned reference checkout')
STAGE.mkdir(parents=True, exist_ok=True)
shutil.copytree(REFERENCE / "fpga/sys", STAGE / "sys", dirs_exist_ok=True)
shutil.copytree(REFERENCE / "fpga/rtl/pll", STAGE / "rtl/pll", dirs_exist_ok=True)
shutil.copyfile(REFERENCE / "fpga/rtl/pll.qip", STAGE / "rtl/pll.qip")
shutil.copyfile(REFERENCE / "fpga/rtl/pll.v", STAGE / "rtl/pll.v")
shutil.copyfile(REFERENCE / "LICENSE", STAGE / "LICENSE")
for source in (ROOT / "fpga").rglob("*"):
    if source.is_file():
        dest = STAGE / source.relative_to(ROOT / "fpga")
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
# Preserve the platform's full emu port interface and copyright header.
prefix = (REFERENCE / "fpga/PICO8.sv").read_text().split("assign ADC_BUS", 1)[0]
(STAGE / "TIC80.sv").write_text(prefix + '\n`include "emu_body.svh"\nendmodule\n')
print(f"Prepared Quartus project: {STAGE}")
