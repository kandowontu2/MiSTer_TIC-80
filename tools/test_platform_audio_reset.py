"""Check the actual shared audio-reset RTL and reset-only platform wiring."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from platform_audio_reset import check_override, extract_helper

ROOT = Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rtl-source', type=Path, default=ROOT / 'fpga/sys/audio_out.v')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--formal', action='store_true')
    parser.add_argument('--expect-failure', action='store_true')
    args = parser.parse_args()
    if args.evidence:
        args.evidence.mkdir(parents=True, exist_ok=False)
    result = dict(started_at=datetime.now(timezone.utc).isoformat(), passed=False,
                  formal=args.formal, expected_failure=args.expect_failure,
                  hardware_accessed=False, physical_qualified=False, sources={})
    with tempfile.TemporaryDirectory(prefix='tic80-platform-reset-') as temp:
        work = Path(temp)
        try:
            pinned = ROOT / 'reference/pico8/fpga/sys/audio_out.v'
            text = args.rtl_source.read_text()
            result['sources']['audio_out.v'] = sha(args.rtl_source)
            result['sources']['pinned_audio_out.v'] = sha(pinned)
            if not args.expect_failure:
                result['wiring'] = check_override(pinned.read_text(), text)
            helper = extract_helper(text)
            (work / 'tic80_audio_reset.v').write_text(helper)
            result['extracted_helper_sha256'] = sha(work / 'tic80_audio_reset.v')
            if args.evidence:
                shutil.copyfile(args.rtl_source, args.evidence / 'audio_out.v')
                shutil.copyfile(work / 'tic80_audio_reset.v', args.evidence / 'tic80_audio_reset.v')
            def run(name, command):
                r = subprocess.run(command, cwd=work, text=True, capture_output=True)
                if args.evidence:
                    (args.evidence / (name + '.log')).write_text(r.stdout + r.stderr)
                return r
            if args.formal:
                fixture = ROOT / 'tests/platform_audio_reset_formal.sv'
                shutil.copyfile(fixture, work / fixture.name)
                script = ('read_verilog -formal -sv tic80_audio_reset.v platform_audio_reset_formal.sv; '
                    'prep -top platform_audio_reset_formal -flatten; async2sync; '
                    'sat -seq 6 -prove-asserts -set-init-def -set-def-inputs -show reset_async,reset_audio '
                    '-dump_vcd witness.vcd')
                if not args.expect_failure:
                    script += ' -tempinduct -tempinduct-def -verify'
                r = run('proof', ['yosys', '-p', script])
                assert r.returncode == 0, r.stderr[-2000:]
                marker = 'SAT proof finished - model found: FAIL!' if args.expect_failure else 'Induction step proven: SUCCESS!'
                assert marker in r.stdout, r.stdout[-2000:]
                result.update(unbounded_proof=not args.expect_failure, counterexample_found=args.expect_failure,
                    scope='Digital rising-edge release after async2sync abstraction; subcycle assertion is tested separately.')
                if args.evidence:
                    (args.evidence / 'proof.ys').write_text(script + '\n')
                    if (work / 'witness.vcd').exists():
                        shutil.copyfile(work / 'witness.vcd', args.evidence / 'witness.vcd')
            else:
                fixture = work / 'platform_audio_reset_fixture.sv'
                fixture.write_text('module platform_audio_reset_fixture(input clk, reset_async, output reset_audio);\n'
                    'tic80_audio_reset dut(.clk(clk), .reset_async(reset_async), .reset_audio(reset_audio));\nendmodule\n')
                test = ROOT / 'tests/platform_audio_reset_test.cpp'
                shutil.copyfile(test, work / test.name)
                r = run('generate', ['verilator', '--cc', '--exe', '--top-module', 'platform_audio_reset_fixture',
                    '-Wno-fatal', '--Mdir', 'obj_dir', 'tic80_audio_reset.v', fixture.name, test.name])
                assert r.returncode == 0, r.stderr[-2000:]
                r = run('build', ['make', '-C', 'obj_dir', '-f', 'Vplatform_audio_reset_fixture.mk', '-j2'])
                assert r.returncode == 0, r.stderr[-2000:]
                r = run('simulation', [str(work / 'obj_dir/Vplatform_audio_reset_fixture')])
                if args.expect_failure:
                    assert r.returncode == 1 and 'RESET_CONTRACT_FAIL' in r.stderr, r.stdout + r.stderr
                    result['counterexample_found'] = True
                else:
                    assert r.returncode == 0, r.stdout + r.stderr
                    result['simulation'] = json.loads(r.stdout)
                    assert result['simulation']['passed']
                if args.evidence:
                    shutil.copyfile(fixture, args.evidence / fixture.name)
            result['sources'][fixture.name] = sha(fixture)
            if not args.formal:
                result['sources'][test.name] = sha(test)
            if args.evidence:
                shutil.copyfile(fixture, args.evidence / fixture.name)
            result['passed'] = True
        except BaseException as error:
            result['error'] = type(error).__name__ + ': ' + str(error)
            raise
        finally:
            if args.evidence:
                (args.evidence / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
