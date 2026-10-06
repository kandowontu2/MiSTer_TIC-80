"""Check reload resets against the actual pinned MiSTer DDR safe terminator."""
import argparse
import hashlib
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--top-source', type=Path, help='Retained pre-fix core top for a counterexample run')
p.add_argument('--terminator-source', type=Path, help='Retained pre-fix terminator for a counterexample run')
p.add_argument('--write-width', type=int, choices=(64, 128), help='Exercise complete cancelled write bursts on a platform port')
args = p.parse_args()
platform = ROOT / 'reference/pico8/fpga/sys'
system = (platform / 'sysmem.sv').read_text()
assert 'ram1_reset_0 <= reset_out;\n\tram1_reset_1 <= ram1_reset_0;' in system
assert '.rst_req_sync             (ram1_reset_1)' in system
rtl = ['fpga/rtl/' + name for name in ('tic80_video_control.sv', 'tic80_ddr_video.sv',
    'tic80_scanout.sv', 'tic80_audio.sv', 'tic80_cart_loader.sv', 'tic80_input.sv', 'tic80_video_top.sv')]
with tempfile.TemporaryDirectory(prefix='tic80-reset-handoff-') as temp:
    work = Path(temp)
    top = 'ddr_terminator_write' if args.write_width else 'ddr_reset_handoff'
    if args.write_width:
        rtl = []
    for name in [*rtl, 'fpga/rtl/memory_map.svh', 'tests/' + top + '.sv',
                 'tests/' + top + '_test.cpp', 'include/tic80_mister/memory_map.h']:
        target = work / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.top_source if args.top_source and name.endswith('tic80_video_top.sv') else ROOT / name, target)
    if args.top_source and not args.write_width:
        # Retained counterexamples predate the newer top-level ports. Keep the
        # exact older logic, adapting only its fixture's named connections.
        old_top = args.top_source.read_text()
        fixture = work / 'tests/ddr_reset_handoff.sv'
        code = fixture.read_text()
        if 'reset_sys_active' not in old_top:
            code = code.replace('        .reset_sys_active(), .reset_vid_active(),\n', '')
        if 'horizontal_wheel' not in old_top:
            code = code.replace("        .horizontal_wheel(32'd0),\n", '')
        fixture.write_text(code)
    original_terminator = platform / 'f2sdram_safe_terminator.sv'
    assert hashlib.sha256(original_terminator.read_bytes()).hexdigest() == '605cd81a43eb94eb34b4917455e7a4a2dbd87b2616ab8a026fb24f3cd9664d86'
    terminator = args.terminator_source or ROOT / 'fpga/sys/f2sdram_safe_terminator.sv'
    digest = hashlib.sha256(terminator.read_bytes()).hexdigest()
    # Quartus accepts procedural assignments to these untyped output/wire
    # declarations; Verilator requires variable declarations. The logic and
    # reset/transaction state machine are unchanged in this temporary copy.
    code = terminator.read_text()
    assert code.count('wire next_state_write;') == 1
    code = code.replace('wire next_state_write;', 'logic next_state_write;')
    code = re.sub(r'(^\s*output\s+)', r'\1logic ', code, flags=re.M)
    code = code.replace("waitrequest_master ? 1'd0 : 1'd1", "waitrequest_master ? 8'd0 : 8'd1")
    (work / 'tests/f2sdram_safe_terminator.sv').write_text(code)
    print('Tested DDR terminator SHA256:', digest, flush=True)
    parameters = ['-GWIDTH=' + str(args.write_width)] if args.write_width else []
    subprocess.run(['verilator', '--cc', '--exe', '--top-module', top, *parameters,
        '--Mdir', 'obj_dir', '-Ifpga/rtl', '-CFLAGS', '-I../include',
        *rtl, 'tests/f2sdram_safe_terminator.sv', 'tests/' + top + '.sv',
        'tests/' + top + '_test.cpp'], cwd=work, check=True)
    subprocess.run(['make', '-C', 'obj_dir', '-f', 'V' + top + '.mk', '-j4'], cwd=work, check=True)
    subprocess.run([str(work / ('obj_dir/V' + top))], cwd=work, check=True)
