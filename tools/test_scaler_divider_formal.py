"""Prove the actual second-stage VHDL for every defined input bit pattern.

GHDL synthesizes the production and pinned arithmetic. Yosys checks all 21
remainder bits, 12 size bits and 12 incoming direction bits without assumptions.
The source guard separately requires unchanged registers, later stages and
pipeline logic. This is an arithmetic proof, not a timing qualification.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, shutil, subprocess, tempfile
from scaler_divider_regions import ROOT, BASELINE, DECLARATIONS, load_regions, stage_entity

parser = argparse.ArgumentParser()
parser.add_argument('--candidate', type=Path, default=ROOT/'fpga/sys/ascal.vhd')
parser.add_argument('--evidence-directory', type=Path)
args = parser.parse_args()
old_bodies, new_bodies = load_regions(args.candidate)

def prove(work):
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    shutil.copyfile(args.candidate, work/'ascal.vhd')
    subprocess.run(['ghdl','-a','--std=08','-frelaxed-rules','ascal.vhd'],cwd=work,check=True)
    for name, body, extra in [('old_divider',old_bodies[1],''),
                               ('new_divider',new_bodies[1],''.join(DECLARATIONS))]:
        (work/(name+'.vhd')).write_text(stage_entity(name,body,1,extra))
        with (work/(name+'.v')).open('w') as output:
            subprocess.run(['ghdl','--synth','--std=08','--out=verilog',name+'.vhd','-e',name],
                           cwd=work,stdout=output,check=True)
    (work/'miter.v').write_text('''module miter(
input [20:0] remainder_in, input [11:0] direction_in, input [11:0] size,
output [20:0] old_remainder, new_remainder,
output [11:0] old_direction, new_direction, output all_equal);
old_divider baseline(remainder_in,direction_in,size,old_remainder,old_direction);
new_divider candidate(remainder_in,direction_in,size,new_remainder,new_direction);
assign all_equal = (old_remainder == new_remainder) && (old_direction == new_direction);
endmodule
''')
    program = ('read_verilog old_divider.v new_divider.v miter.v; '
               'prep -top miter -flatten; '
               'sat -verify -prove all_equal 1 -show-inputs -show-outputs '
               '-timeout 90 -dump_json counterexample.json')
    proc = subprocess.run(['yosys','-Q','-T','-p',program],cwd=work,
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (work/'yosys-proof.log').write_text(proc.stdout)
    print(proc.stdout)
    record = dict(recorded_at=datetime.now(timezone.utc).isoformat(),
                  candidate_sha256=sha(args.candidate),baseline_sha256=sha(BASELINE),
                  stage=2,unrestricted_input_bits=45,output_bits_compared=33,
                  assumptions=[],outside_arithmetic_regions_unchanged=True,
                  command=program,exit_code=proc.returncode,
                  passed=proc.returncode==0 and 'SAT proof finished - no model found: SUCCESS!' in proc.stdout,
                  sources={n:sha(work/n) for n in ('ascal.vhd','old_divider.vhd','new_divider.vhd',
                                                  'old_divider.v','new_divider.v','miter.v')},
                  tool_versions={name:subprocess.check_output([name,'--version' if name=='ghdl' else '-V'],
                                                               text=True).strip() for name in ('ghdl','yosys')})
    (work/'proof-result.json').write_text(json.dumps(record,indent=2)+'\n')
    assert record['passed'], 'Actual VHDL second-stage arithmetic proof failed; inspect yosys-proof.log'
    print('SCALER_SECOND_STAGE_FORMAL_PASS unrestricted_input_bits=45 output_bits=33 assumptions=0')

if args.evidence_directory:
    args.evidence_directory.mkdir(parents=True,exist_ok=False)
    prove(args.evidence_directory.resolve())
else:
    with tempfile.TemporaryDirectory(prefix='tic80-scaler-formal-') as folder:
        prove(Path(folder))
