"""Bind a timing-checked RBF to its staged sources and fresh audit evidence."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--enable-yc', action='store_true')
parser.add_argument('--stage', type=Path, default=root / 'build/fpga')
parser.add_argument('--evidence-directory', type=Path, default=root / 'build')
parser.add_argument('--output', type=Path, default=root / 'build/fpga-build.json')
args = parser.parse_args()
stage = args.stage.resolve()
rbf = stage / 'output_files/TIC80.rbf'

def digest(file): return hashlib.sha256(file.read_bytes()).hexdigest()

sources = {}
for source in sorted((root / 'fpga').rglob('*')):
    if source.is_file():
        relative = source.relative_to(root / 'fpga')
        assert digest(source) == digest(stage / relative), ('Source changed after staging', str(relative))
        sources[str(source.relative_to(root)).replace('\\', '/')] = digest(source)

subprocess.run([sys.executable, str(root / 'tools/check_timing.py'),
                str(stage / 'output_files/TIC80.sta.summary')], check=True)
audits = {}
audio_reset_tables = {}
required_audits = [
    ('audio-cdc-audit.log', {'AUDIO_CDC_PASS ': 20, 'CONTROL_CDC_PASS ': 24}),
    ('video-clock-audit.log', {'SHARED_CLOCK_PASS ': 4}),
    ('ddr-reset-postfit-audit.log', {'RESET_PIPELINE_PASS ': 3, 'RESET_PLATFORM_PASS ': 1})]
body = root / 'fpga/emu_body.svh'
shared_reset = body.exists() and '.reset(reset_sys_active)' in body.read_text() and '.reset(reset_vid_active)' in body.read_text()
if shared_reset:
    required_audits.append(('shared-reset-postfit-audit.log', {'SHARED_RESET_PASS ': 8}))
explicit_sync = (root / 'fpga/cdc.tcl').exists() and 'source cdc.tcl' in (root / 'fpga/TIC80.qsf').read_text()
if explicit_sync:
    required_audits.append(('core-synchronizers-postfit-audit.log', {'CORE_SYNC_PASS ': 4, 'CORE_MTBF_PASS ': 4}))
    subprocess.run([sys.executable, str(root / 'tools/check_core_synchronizers.py'),
                    str(args.evidence_directory)], check=True)
audio_override = root / 'fpga/sys/audio_out.v'
platform_audio_reset = audio_override.exists() and 'module tic80_audio_reset' in audio_override.read_text()
if platform_audio_reset:
    from platform_audio_reset import check_override, check_consumer_table
    check_override((root / 'reference/pico8/fpga/sys/audio_out.v').read_text(), audio_override.read_text())
    required_audits.append(('platform-audio-reset-postfit-audit.log', {'PLATFORM_AUDIO_RESET_PASS ': 4}))
for name, markers in required_audits:
    file = args.evidence_directory / name
    assert file.stat().st_mtime_ns >= rbf.stat().st_mtime_ns, ('Stale post-fit audit', name)
    text = file.read_text()
    for marker, count in markers.items(): assert text.count(marker) == count, (name, marker)
    if name == 'shared-reset-postfit-audit.log':
        lines = [line for line in text.splitlines() if line.startswith('SHARED_RESET_PASS ')]
        assert all('raw_paths=0' in line for line in lines), 'Raw reset path remains'
    if name == 'platform-audio-reset-postfit-audit.log':
        lines = [line for line in text.splitlines() if line.startswith('PLATFORM_AUDIO_RESET_PASS ')]
        corners = set()
        for line in lines:
            match = re.fullmatch(r'PLATFORM_AUDIO_RESET_PASS model=(slow|fast) temperature=(-40|100) '
                                 r'stages=3 consumers=(\d+) raw_paths=0', line)
            assert match and int(match[3]) >= 20, 'Audio reset stage/consumer/bypass audit failed'
            corner = (match[1], match[2])
            assert corner not in corners, 'Duplicate audio reset corner'
            corners.add(corner)
            table = args.evidence_directory / f'platform-audio-reset-{match[1]}-{match[2]}-consumers.tsv'
            evidence = check_consumer_table(table, int(match[3]), rbf.stat().st_mtime_ns)
            audio_reset_tables[table.name] = dict(sha256=digest(table), **evidence)
        assert corners == {(model, temperature) for model in ('slow', 'fast') for temperature in ('-40', '100')}
    audits[name] = digest(file)

result = dict(recorded_at=datetime.now(timezone.utc).isoformat(), rbf_sha256=digest(rbf),
              sources=sources, generated_top_sha256=digest(stage / 'TIC80.sv'),
              timing_report_sha256=digest(stage / 'output_files/TIC80.sta.summary'),
              post_fit_audits=audits, shared_reset_audit_required=shared_reset,
              platform_audio_reset_consumer_tables=audio_reset_tables,
              explicit_synchronizer_audit_required=explicit_sync, platform_audio_reset_audit_required=platform_audio_reset,
              enable_yc=args.enable_yc,
              qualification='Local timing/routing checks only; live HDMI clock and panel checks required')
args.output.write_text(json.dumps(result, indent=2) + '\n')
print('FPGA build recorded:', result['rbf_sha256'])
