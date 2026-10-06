"""Build and execute the ARM producer / FPGA ownership-controller co-simulation."""
from pathlib import Path
import subprocess
import shutil
import tempfile
import argparse
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--suite", choices=["control", "ddr", "scanout", "audio", "integration", "video-cdc", "cart", "input", "pixel", "horizontal"], default="control")
parser.add_argument("--rtl-source", type=Path, help="Replace the selected top RTL (DDR reader for video-cdc) for a regression counterexample")
args = parser.parse_args()
top = {"control": "tic80_video_control", "ddr": "tic80_ddr_video", "scanout": "tic80_scanout", "audio": "tic80_audio", "integration": "tic80_video_top", "video-cdc": "video_cdc_fixture", "cart": "tic80_cart_loader", "input": "tic80_input", "pixel": "tic80_pixel_enable", "horizontal": "tic80_horizontal_wheel"}[args.suite]
test = {"control": "video_control_test.cpp", "ddr": "ddr_video_test.cpp", "scanout": "scanout_test.cpp", "audio": "audio_test.cpp", "integration": "integration_test.cpp", "video-cdc": "video_cdc_test.cpp", "cart": "cart_loader_test.cpp", "input": "input_test.cpp", "pixel": "pixel_enable_test.cpp", "horizontal": "horizontal_wheel_test.cpp"}[args.suite]
rtl = ["fpga/rtl/tic80_video_control.sv"]
if args.suite == "ddr":
    rtl.append("fpga/rtl/tic80_ddr_video.sv")
elif args.suite == "scanout":
    rtl = ["fpga/rtl/tic80_scanout.sv"]
elif args.suite == "audio":
    rtl = ["fpga/rtl/tic80_audio.sv"]
elif args.suite in ("integration", "video-cdc"):
    rtl += ["fpga/rtl/tic80_ddr_video.sv", "fpga/rtl/tic80_scanout.sv", "fpga/rtl/tic80_audio.sv", "fpga/rtl/tic80_video_top.sv", "fpga/rtl/tic80_cart_loader.sv", "fpga/rtl/tic80_input.sv"]
    if args.suite == "video-cdc":
        rtl.append("tests/video_cdc_fixture.sv")
elif args.suite == "cart":
    rtl = ["fpga/rtl/tic80_cart_loader.sv"]
elif args.suite == "input":
    rtl = ["fpga/rtl/tic80_input.sv"]
elif args.suite == "pixel":
    rtl = ["fpga/rtl/tic80_pixel_enable.sv"]
elif args.suite == "horizontal":
    rtl = ["fpga/rtl/tic80_horizontal_wheel.sv"]
# Verilator's generated Makefiles do not support spaces in source/build paths.
# Copy only the actual test inputs into a temporary build directory.
with tempfile.TemporaryDirectory(prefix="tic80-rtl-") as temp:
    work = Path(temp)
    for name in [*rtl, "fpga/rtl/memory_map.svh",
                 "tests/" + test, "src/exchange.c",
                 "include/tic80_mister/exchange.h", "include/tic80_mister/memory_map.h"]:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, destination)
    if args.rtl_source:
        replacement = "tic80_ddr_video" if args.suite == "video-cdc" else top
        shutil.copyfile(args.rtl_source, work / ("fpga/rtl/" + replacement + ".sv"))
    subprocess.run([
        "verilator", "--cc", "--exe", "--top-module", top,
        "--Mdir", "obj_dir", "-Ifpga/rtl", "-CFLAGS", "-I../include",
        *rtl, "tests/" + test, "src/exchange.c",
    ], check=True, cwd=work)
    # Keep generation separate from its child build. Verilator 5.020 can
    # abort tearing down its internal thread pool after the combined --build
    # command; the generated sources/build themselves are unaffected.
    subprocess.run(["make", "-C", "obj_dir", "-f", "V" + top + ".mk", "-j4"], check=True, cwd=work)
    subprocess.run([str(work / ("obj_dir/V" + top))], check=True, cwd=work)
