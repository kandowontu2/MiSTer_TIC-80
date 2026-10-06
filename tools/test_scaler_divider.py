"""Compare the actual modified VHDL divider against the pinned implementation.

Surrounding VHDL is guarded separately from the two permitted arithmetic stages.
This check exhaustively compares the first stage; the second has a formal check.
GHDL synthesizes the extracted, unchanged arithmetic regions; Verilator compares
all 16,384 accumulator values and 4,096 sizes, including modulo/sign boundaries.
"""
from pathlib import Path
import argparse, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--candidate', type=Path, default=ROOT/'fpga/sys/ascal.vhd')
args = parser.parse_args()
from scaler_divider_regions import load_regions, stage_entity, DECLARATIONS
old_bodies, new_bodies = load_regions(args.candidate)
old_body, new_body = old_bodies[0], new_bodies[0]
declaration = ''.join(DECLARATIONS)

with tempfile.TemporaryDirectory(prefix='tic80-scaler-divider-') as folder:
    work = Path(folder)
    # Analyze the full modified production file as well as the isolated regions.
    shutil.copyfile(args.candidate, work/'ascal.vhd')
    subprocess.run(['ghdl','-a','--std=08','-frelaxed-rules','ascal.vhd'],cwd=work,check=True)
    for name, body, extra in [('old_divider',old_body,''),('new_divider',new_body,declaration)]:
        (work/(name+'.vhd')).write_text(stage_entity(name,body,0,extra))
        with (work/(name+'.v')).open('w') as output:
            subprocess.run(['ghdl','--synth','--std=08','--out=verilog',name+'.vhd','-e',name],cwd=work,stdout=output,check=True)
    (work/'fixture.v').write_text('''module scaler_divider_equivalence(
input [13:0] accumulator, input [11:0] size,
output [20:0] old_remainder, new_remainder,
output [11:0] old_direction, new_direction);
old_divider baseline(accumulator,size,old_remainder,old_direction);
new_divider candidate(accumulator,size,new_remainder,new_direction);
endmodule
''')
    shutil.copyfile(ROOT/'tests/scaler_divider_test.cpp',work/'test.cpp')
    subprocess.run(['verilator','--cc','--exe','--top-module','scaler_divider_equivalence','--Mdir','obj_dir',
                    'old_divider.v','new_divider.v','fixture.v','test.cpp'],cwd=work,check=True)
    subprocess.run(['make','-C','obj_dir','-f','Vscaler_divider_equivalence.mk','-j4'],cwd=work,check=True)
    subprocess.run([str(work/'obj_dir/Vscaler_divider_equivalence')],cwd=work,check=True)
