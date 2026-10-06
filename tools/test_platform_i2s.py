"""Simulate the actual pinned platform I2S serializer and clock enables."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from platform_audio_initializers import normalize

ROOT = Path(__file__).resolve().parents[1]
MODULES = ('audio_out.v', 'i2s.v', 'iir_filter.v', 'sigma_delta_dac.v', 'spdif.v')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform-source', type=Path, default=ROOT / 'reference/pico8/fpga/sys')
    parser.add_argument('--rtl-source', type=Path, help='Alternate actual i2s.v for a negative regression')
    parser.add_argument('--audio-rtl-source', type=Path, help='Actual audio_out.v override with synchronized reset')
    parser.add_argument('--require-safe-reset', action='store_true', help='Check edge spacing through active reset as well as playback')
    parser.add_argument('--evidence', type=Path, help='Create fresh source/log evidence; never overwrite a previous run')
    args = parser.parse_args()
    if args.evidence:
        args.evidence.mkdir(parents=True, exist_ok=False)
    result = dict(started_at=datetime.now(timezone.utc).isoformat(), passed=False,
                  hardware_accessed=False, external_qualified=False, require_safe_reset=args.require_safe_reset, sources={})
    with tempfile.TemporaryDirectory(prefix='tic80-platform-i2s-') as temp:
        work = Path(temp)
        inputs = {name: args.platform_source / name for name in MODULES}
        if args.rtl_source:
            inputs['i2s.v'] = args.rtl_source
        if args.audio_rtl_source:
            inputs['audio_out.v'] = args.audio_rtl_source
        inputs.update({'platform_i2s_fixture.sv': ROOT / 'tests/platform_i2s_fixture.sv',
                       'platform_i2s_test.cpp': ROOT / 'tests/platform_i2s_test.cpp'})
        for name, source in inputs.items():
            shutil.copyfile(source, work / name)
            result['sources'][name] = dict(sha256=sha(source), bytes=source.stat().st_size)
            if args.evidence:
                destination = args.evidence / 'source' / name
                destination.parent.mkdir(exist_ok=True)
                shutil.copyfile(source, destination)
        if args.audio_rtl_source:
            # Observe the reset actually sampled by the downstream serializer.
            # This alias changes no production logic or reset-service assertion.
            fixture = work / 'platform_i2s_fixture.sv'
            body = fixture.read_text()
            marker = 'assign active_reset = reset;'
            if body.count(marker) != 1:
                raise ValueError('Audio-reset observation marker changed')
            body = body.replace(marker, 'assign active_reset = dut.reset_audio;')
            fixture.write_text(body)
            result['audio_reset_observation'] = dict(production_logic_changed=False, fixture_sha256=sha(fixture))
            if args.evidence:
                (args.evidence / 'generated_platform_i2s_fixture.sv').write_text(body)
        original = (work / 'audio_out.v').read_text()
        normalized, fields = normalize(original)
        (work / 'audio_out.v').write_text(normalized)
        result['simulator_initializer_conversion'] = dict(fields=fields,
            original_sha256=result['sources']['audio_out.v']['sha256'],
            normalized_sha256=sha(work / 'audio_out.v'),
            normalizer_sha256=sha(ROOT / 'tools/platform_audio_initializers.py'),
            product_RTL_changed=False)
        if args.evidence:
            (args.evidence / 'normalized').mkdir()
            shutil.copyfile(work / 'audio_out.v', args.evidence / 'normalized/audio_out.v')
        def run(name, command):
            completed = subprocess.run(command, cwd=work, text=True, capture_output=True)
            if args.evidence:
                (args.evidence / (name + '.log')).write_text(completed.stdout + completed.stderr)
            if completed.returncode:
                raise RuntimeError(f'{name} exited {completed.returncode}: ' + (completed.stderr or completed.stdout)[-2000:])
            return completed.stdout
        try:
            result['verilator_version'] = run('version', ['verilator', '--version']).strip()
            run('generate', ['verilator', '--cc', '--exe', '--top-module', 'platform_i2s_fixture',
                            '-Wno-fatal', '--Mdir', 'obj_dir', *MODULES,
                            'platform_i2s_fixture.sv', 'platform_i2s_test.cpp'])
            run('build', ['make', '-C', 'obj_dir', '-f', 'Vplatform_i2s_fixture.mk', '-j2'])
            simulation_args = ['--require-safe-reset'] if args.require_safe_reset else []
            result['simulation'] = json.loads(run('simulation', [str(work / 'obj_dir/Vplatform_i2s_fixture'), *simulation_args]))
            assert result['simulation']['passed'] and not result['simulation']['external_qualified']
            result['passed'] = True
        except BaseException as error:
            result['error'] = type(error).__name__ + ': ' + str(error)
            raise
        finally:
            if args.evidence:
                (args.evidence / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['simulation']))


if __name__ == '__main__':
    main()
