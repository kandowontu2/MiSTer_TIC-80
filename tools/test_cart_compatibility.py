"""Verify the accepted legacy loader with new Main's metadata packets."""
from pathlib import Path
import argparse
import hashlib
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--legacy-rtl',type=Path,required=True)
args=parser.parse_args()
assert hashlib.sha256(args.legacy_rtl.read_bytes()).hexdigest()=='223a594f01af256de3d304adca8e6d44c35a73f1bb2683e6550307ec880b98a1','Not the accepted legacy cart loader'
with tempfile.TemporaryDirectory(prefix='tic80-source-legacy-') as directory:
    work=Path(directory)
    for name in ('fpga/rtl/memory_map.svh','tests/cart_legacy_source_test.cpp','tools/main_source_packet.h','include/tic80_mister/memory_map.h'):
        dest=work/name; dest.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(ROOT/name,dest)
    shutil.copyfile(args.legacy_rtl,work/'fpga/rtl/tic80_cart_loader.sv')
    subprocess.run(['verilator','--cc','--exe','--top-module','tic80_cart_loader','--Mdir','obj_dir','-Ifpga/rtl',
                    '-CFLAGS','-I../include','fpga/rtl/tic80_cart_loader.sv','tests/cart_legacy_source_test.cpp'],cwd=work,check=True)
    subprocess.run(['make','-C','obj_dir','-f','Vtic80_cart_loader.mk','-j4'],cwd=work,check=True)
    subprocess.run([str(work/'obj_dir/Vtic80_cart_loader')],check=True,cwd=work)
