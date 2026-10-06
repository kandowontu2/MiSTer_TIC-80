"""Prove safe I2S data-update phases for arbitrary reset and stereo samples."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def include_platform_enable(work, evidence, result, platform):
    text = platform.read_text()
    match = re.search(r'^reg i2s_ce;\s*always @\(posedge clk\) begin\n.*?(?=\ni2s i2s\n)',
                      text, re.M | re.S)
    if not match:
        raise RuntimeError('Exact platform enable divider absent')
    divider = match[0]
    expected_divider = ('reg i2s_ce;\nalways @(posedge clk) begin\n\treg div;\n'
        '\ti2s_ce <= 0;\n\tif(mclk_ce) begin\n\t\tdiv <= ~div;\n'
        '\t\ti2s_ce <= div;\n\tend\nend\n')
    if divider != expected_divider:
        raise RuntimeError('Platform enable-divider source changed; review required')
    wrapper_path = work / 'platform_i2s_reset_formal.sv'
    wrapper = wrapper_path.read_text()
    input_declaration = 'input clk, reset, ce,'
    assumption = 'assume(!ce || !$past(ce));'
    insertion = '    wire sclk, lrclk, sdata;'
    if any(wrapper.count(item) != 1 for item in (input_declaration, assumption, insertion)):
        raise RuntimeError('Formal wrapper structure changed; review required')
    wrapper = wrapper.replace(input_declaration, 'input clk, reset, mclk_ce,')
    wrapper = wrapper.replace(insertion, divider + 'wire ce = i2s_ce;\n' + insertion)
    wrapper = wrapper.replace(
        '// The platform clock-enable generator never emits adjacent pulses.\n'
        '            // No reset, audio-word or longer enable-spacing restrictions apply.\n'
        '            ' + assumption,
        '// The exact platform divider is included with arbitrary defined\n'
        '            // initial states. Its spacing invariant holds after two samples.\n'
        '            if (past_valid[1]) assert(!ce || !$past(ce));')
    if 'assume(' in wrapper or wrapper.count('assert(') != 7:
        raise RuntimeError('Unexpected generated proof assumptions or assertions')
    wrapper_path.write_text(wrapper)
    result['platform_enable_included'] = True
    result['sources']['audio_out.v'] = hashlib.sha256(platform.read_bytes()).hexdigest()
    result['generated_wrapper_sha256'] = hashlib.sha256(wrapper_path.read_bytes()).hexdigest()
    if evidence:
        shutil.copyfile(platform, evidence / 'audio_out.v')
        shutil.copyfile(wrapper_path, evidence / 'generated_platform_i2s_reset_formal.sv')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rtl-source', type=Path, default=ROOT / 'fpga/sys/i2s.v')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--expect-counterexample', action='store_true')
    parser.add_argument('--include-platform-enable', action='store_true',
                        help='Include the exact pinned enable divider instead of assuming enable spacing')
    override = ROOT / 'fpga/sys/audio_out.v'
    parser.add_argument('--platform-source', type=Path,
                        default=override if override.exists() else ROOT / 'reference/pico8/fpga/sys/audio_out.v',
                        help='Actual audio_out.v supplying the byte-checked enable divider')
    args = parser.parse_args()
    if args.evidence:
        args.evidence.mkdir(parents=True, exist_ok=False)
    result = dict(started_at=datetime.now(timezone.utc).isoformat(), passed=False,
                  expected_counterexample=args.expect_counterexample, hardware_accessed=False,
                  external_qualified=False, sources={})
    with tempfile.TemporaryDirectory(prefix='tic80-i2s-reset-proof-') as temp:
        work = Path(temp)
        for name, source in (('i2s.v', args.rtl_source),
                             ('platform_i2s_reset_formal.sv', ROOT / 'tests/platform_i2s_reset_formal.sv')):
            shutil.copyfile(source, work / name)
            result['sources'][name] = hashlib.sha256(source.read_bytes()).hexdigest()
            if args.evidence:
                shutil.copyfile(source, args.evidence / name)
        script = ('read_verilog -formal -sv i2s.v platform_i2s_reset_formal.sv; '
                  'prep -top platform_i2s_reset_formal -flatten; '
                  'sat -seq 8 -prove-asserts -set-assumes -set-init-def -set-def-inputs '
                  '-show reset,ce,sclk,lrclk,sdata -dump_vcd witness.vcd')
        if not args.expect_counterexample:
            script += ' -tempinduct -tempinduct-def -verify'
        try:
            if args.include_platform_enable:
                include_platform_enable(work, args.evidence, result, args.platform_source)
            run = subprocess.run(['yosys', '-p', script], cwd=work, text=True, capture_output=True)
            log = run.stdout + run.stderr
            if args.evidence:
                (args.evidence / 'proof.ys').write_text(script + '\n')
                (args.evidence / 'proof.log').write_text(log)
                if (work / 'witness.vcd').exists():
                    shutil.copyfile(work / 'witness.vcd', args.evidence / 'witness.vcd')
            if run.returncode:
                raise RuntimeError(f'Yosys exited {run.returncode}: {log[-1500:]}')
            expected = ('SAT proof finished - model found: FAIL!' if args.expect_counterexample
                        else 'Induction step proven: SUCCESS!')
            if expected not in log:
                raise RuntimeError('Expected proof result absent: ' + log[-1500:])
            result.update(passed=True, unbounded_proof=not args.expect_counterexample,
                          counterexample_found=args.expect_counterexample,
                          minimum_change_to_bclk_rise_base_cycles=None if args.expect_counterexample else 3,
                          minimum_bclk_rise_to_change_base_cycles=None if args.expect_counterexample else 1,
                          assumption=('None: exact platform enable divider included; reset, mclk_ce and stereo words unrestricted'
                                      if args.include_platform_enable else
                                      'No adjacent clock-enable pulses; reset and both stereo words are otherwise unrestricted'))
        except BaseException as error:
            result['error'] = type(error).__name__ + ': ' + str(error)
            raise
        finally:
            if args.evidence:
                (args.evidence / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
