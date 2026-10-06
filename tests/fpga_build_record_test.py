"""Reject stale build evidence before a bitstream can become installable."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildRecord(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        for name in ('tools', 'fpga', 'build/fpga/output_files'):
            (self.root / name).mkdir(parents=True)
        for name in ('record_fpga_build.py', 'check_timing.py', 'check_core_synchronizers.py', 'platform_audio_reset.py'):
            shutil.copyfile(ROOT / 'tools' / name, self.root / 'tools' / name)
        for name in ('fpga/design.sv', 'build/fpga/design.sv', 'build/fpga/TIC80.sv'):
            (self.root / name).write_text('module design; endmodule\n')
        self.rbf = self.root / 'build/fpga/output_files/TIC80.rbf'
        self.rbf.write_bytes(b'candidate A')
        self.summary = self.root / 'build/fpga/output_files/TIC80.sta.summary'
        self.summary.write_text(''.join(
            f'Type : {kind}\nSlack : 0.100\nTNS : 0.000\n\n'
            for kind in ('Setup', 'Hold', 'Recovery', 'Removal', 'Minimum Pulse Width')))
        for name, contents in [
            ('audio-cdc-audit.log', 'AUDIO_CDC_PASS x\n' * 20 + 'CONTROL_CDC_PASS x\n' * 24),
            ('video-clock-audit.log', 'SHARED_CLOCK_PASS x\n' * 4),
            ('ddr-reset-postfit-audit.log', 'RESET_PIPELINE_PASS x\n' * 3 + 'RESET_PLATFORM_PASS x\n')]:
            (self.root / 'build' / name).write_text(contents)
        os.utime(self.rbf, (100, 100))

    def run_record(self, success):
        result = subprocess.run([sys.executable, str(self.root / 'tools/record_fpga_build.py')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertEqual((self.root / 'build/fpga-build.json').exists(), success)
        return result

    def test_success_binds_exact_artifact(self):
        self.run_record(True)
        record = json.loads((self.root / 'build/fpga-build.json').read_text())
        self.assertEqual(record['rbf_sha256'], hashlib.sha256(self.rbf.read_bytes()).hexdigest())
        self.assertEqual(record['sources']['fpga/design.sv'],
                         hashlib.sha256((self.root / 'fpga/design.sv').read_bytes()).hexdigest())

    def test_separate_candidate_preserves_default_receipt(self):
        self.run_record(True)
        baseline = (self.root / 'build/fpga-build.json').read_bytes()
        separate = self.root / 'build/separate-candidate'
        shutil.copytree(self.root / 'build/fpga', separate / 'fpga')
        for name in ('audio-cdc-audit.log', 'video-clock-audit.log', 'ddr-reset-postfit-audit.log'):
            shutil.copyfile(self.root / 'build' / name, separate / name)
        result = subprocess.run([sys.executable, str(self.root / 'tools/record_fpga_build.py'),
            '--stage', str(separate / 'fpga'), '--evidence-directory', str(separate),
            '--output', str(separate / 'fpga-build.json')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'build/fpga-build.json').read_bytes(), baseline)
        self.assertEqual((separate / 'fpga-build.json').exists(), True)

    def test_changed_source_cannot_use_previous_fit(self):
        (self.root / 'fpga/design.sv').write_text('module changed; endmodule\n')
        self.run_record(False)

    def test_explicit_sync_cannot_use_marker_only_evidence(self):
        for folder in ('fpga', 'build/fpga'):
            (self.root / folder / 'cdc.tcl').write_text('# explicit custom chains\n')
            (self.root / folder / 'TIC80.qsf').write_text('source cdc.tcl\n')
        # Even plausible pass markers cannot replace the per-chain fitted
        # reports checked by the actual validator.
        (self.root / 'build/core-synchronizers-postfit-audit.log').write_text(
            'CORE_SYNC_PASS model=slow first_stages=117\n' * 4 +
            'CORE_MTBF_PASS corner=slow first_stages=117\n' * 4)
        result = self.run_record(False)
        self.assertIn('core-sync-slow--40.tsv', result.stderr)

    def test_failed_timing_cannot_use_positive_audits(self):
        # Observed cold HDMI setup failure from the seed-10 full-controller fit.
        self.summary.write_text(self.summary.read_text().replace(
            'Type : Setup\nSlack : 0.100\nTNS : 0.000',
            'Type : Setup\nSlack : -0.131\nTNS : -0.181'))
        result = self.run_record(False)
        self.assertIn('VIOLATION:', result.stdout)

    def test_new_artifact_cannot_use_old_audits(self):
        self.rbf.write_bytes(b'candidate B')
        os.utime(self.rbf, (200, 200))
        os.utime(self.root / 'build/audio-cdc-audit.log', (100, 100))
        self.run_record(False)

    def test_incomplete_crossing_audit_is_rejected(self):
        (self.root / 'build/audio-cdc-audit.log').write_text('AUDIO_CDC_PASS x\n' * 19)
        self.run_record(False)

    def test_incomplete_reset_handoff_audit_is_rejected(self):
        (self.root / 'build/ddr-reset-postfit-audit.log').write_text('RESET_PIPELINE_PASS x\n' * 2 + 'RESET_PLATFORM_PASS x\n')
        self.run_record(False)

    def shared_reset_source(self):
        for folder in ('fpga','build/fpga'):
            (self.root / folder / 'emu_body.svh').write_text('.reset(reset_sys_active)\n.reset(reset_vid_active)\n')

    def test_shared_reset_requires_new_audit(self):
        self.shared_reset_source()
        self.run_record(False)

    def test_shared_reset_binds_new_audit(self):
        self.shared_reset_source()
        audit = self.root / 'build/shared-reset-postfit-audit.log'
        audit.write_text('SHARED_RESET_PASS raw_paths=0\n' * 8)
        self.run_record(True)
        record = json.loads((self.root / 'build/fpga-build.json').read_text())
        self.assertTrue(record['shared_reset_audit_required'])
        self.assertEqual(record['post_fit_audits'][audit.name], hashlib.sha256(audit.read_bytes()).hexdigest())

    def test_shared_reset_with_raw_path_is_rejected(self):
        self.shared_reset_source()
        (self.root / 'build/shared-reset-postfit-audit.log').write_text(
            'SHARED_RESET_PASS raw_paths=0\n' * 7 + 'SHARED_RESET_PASS raw_paths=1\n')
        self.run_record(False)

    def audio_reset_source(self):
        reference = self.root / 'reference/pico8/fpga/sys'
        reference.mkdir(parents=True)
        shutil.copyfile(ROOT / 'reference/pico8/fpga/sys/audio_out.v', reference / 'audio_out.v')
        for folder in ('fpga/sys', 'build/fpga/sys'):
            (self.root / folder).mkdir(parents=True)
            shutil.copyfile(ROOT / 'fpga/sys/audio_out.v', self.root / folder / 'audio_out.v')

    def audio_reset_audit(self, raw_paths=0):
        audit = self.root / 'build/platform-audio-reset-postfit-audit.log'
        audit.write_text(''.join(f'PLATFORM_AUDIO_RESET_PASS model={model} temperature={temperature} '
            f'stages=3 consumers=100 raw_paths={raw_paths}\n'
            for model in ('slow', 'fast') for temperature in (-40, 100)))
        clock = 'pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk'
        for model in ('slow', 'fast'):
            for temperature in (-40, 100):
                table = self.root / f'build/platform-audio-reset-{model}-{temperature}-consumers.tsv'
                table.write_text('sink\tcheck\tlaunch_clock\tlatch_clock\tslack_ns\n' + ''.join(
                    f'audio|consumer[{index}]\t{mode}\t{clock}\t{clock}\t0.250\n'
                    for index in range(100) for mode in ('recovery', 'removal')))
        return audit

    def test_audio_reset_marker_without_inventory_is_rejected(self):
        self.audio_reset_source()
        self.audio_reset_audit()
        (self.root / 'build/platform-audio-reset-slow--40-consumers.tsv').unlink()
        self.run_record(False)

    def test_audio_reset_incomplete_or_invalid_inventory_is_rejected(self):
        self.audio_reset_source()
        self.audio_reset_audit()
        table = self.root / 'build/platform-audio-reset-slow--40-consumers.tsv'
        original = table.read_text()
        for invalid in (
            original.rsplit('\n', 2)[0] + '\n',
            original.replace('\tremoval\t', '\tsetup\t', 1),
            original.replace('\t0.250\n', '\tnan\n', 1),
            original.replace('\t0.250\n', '\t-0.001\n', 1),
            original.replace('pll_audio|', 'pll_sys|', 1),
            original + original.splitlines()[1] + '\n',
        ):
            with self.subTest(invalid=invalid[-80:]):
                table.write_text(invalid)
                self.run_record(False)

    def test_audio_reset_stale_inventory_is_rejected(self):
        self.audio_reset_source()
        self.audio_reset_audit()
        os.utime(self.root / 'build/platform-audio-reset-slow--40-consumers.tsv', (90, 90))
        self.run_record(False)

    def test_audio_reset_requires_fresh_audit(self):
        self.audio_reset_source()
        self.run_record(False)
        audit = self.audio_reset_audit()
        os.utime(audit, (90, 90))
        self.run_record(False)

    def test_audio_reset_binds_fitted_audit(self):
        self.audio_reset_source()
        audit = self.audio_reset_audit()
        self.run_record(True)
        record = json.loads((self.root / 'build/fpga-build.json').read_text())
        self.assertTrue(record['platform_audio_reset_audit_required'])
        self.assertEqual(record['post_fit_audits'][audit.name], hashlib.sha256(audit.read_bytes()).hexdigest())

    def test_audio_reset_raw_path_is_rejected(self):
        self.audio_reset_source()
        self.audio_reset_audit(raw_paths=1)
        self.run_record(False)

    def test_audio_reset_unreviewed_logic_is_rejected(self):
        self.audio_reset_source()
        self.audio_reset_audit()
        for folder in ('fpga/sys', 'build/fpga/sys'):
            path = self.root / folder / 'audio_out.v'
            path.write_text(path.read_text().replace('assign reset_audio = release_pipe[2];',
                                                    'assign reset_audio = reset_async;'))
        self.run_record(False)

    def test_audio_reset_duplicate_corner_is_rejected(self):
        self.audio_reset_source()
        audit = self.audio_reset_audit()
        audit.write_text(audit.read_text().replace('model=fast temperature=100', 'model=slow temperature=100'))
        self.run_record(False)


if __name__ == '__main__':
    unittest.main()
