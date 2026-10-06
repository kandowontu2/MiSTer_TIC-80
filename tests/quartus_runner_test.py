"""Exercise native stderr/exit handling and single-launch log reservation."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[1] / 'tools/run_quartus.py'


class NativeRunner(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.folder = Path(self.directory.name)
        self.log = self.folder / 'native.log'
        self.child = self.folder / 'child.py'
        self.child.write_text("import sys\nprint('stdout retained')\nprint('stderr retained', file=sys.stderr)\nsys.exit(int(sys.argv[1]))\n")

    def run_child(self, status=0, append=False):
        return subprocess.run([sys.executable, str(RUNNER), '--log', str(self.log),
            *(['--append'] if append else []), '--', sys.executable, str(self.child), str(status)],
            capture_output=True, text=True)

    def test_stderr_is_retained_with_actual_exit_status(self):
        result = self.run_child(7)
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertIn('stdout retained', self.log.read_text(encoding='utf-8'))
        self.assertIn('stderr retained', self.log.read_text(encoding='utf-8'))

    def test_utf8_append_retains_previous_output(self):
        self.assertEqual(self.run_child().returncode, 0)
        self.assertEqual(self.run_child(append=True).returncode, 0)
        self.assertEqual(self.log.read_text().count('stderr retained'), 2)

    def test_existing_log_prevents_second_launch(self):
        self.log.write_text('sealed evidence\n')
        marker = self.folder / 'unexpected-launch'
        self.child.write_text(f'from pathlib import Path\nPath({str(marker)!r}).touch()\n')
        self.assertEqual(self.run_child().returncode, 2)
        self.assertFalse(marker.exists())
        self.assertEqual(self.log.read_text(), 'sealed evidence\n')

    def test_mixed_encoding_prevents_append_launch(self):
        self.log.write_bytes('prior output'.encode('utf-16'))
        previous = self.log.read_bytes()
        self.assertEqual(self.run_child(append=True).returncode, 2)
        self.assertEqual(self.log.read_bytes(), previous)

    @unittest.skipUnless(os.name == 'nt', 'Windows PowerShell 5 regression')
    def test_powershell_stop_does_not_treat_native_stderr_as_failure(self):
        quote = lambda value: "'" + str(value).replace("'", "''") + "'"
        script = "$ErrorActionPreference='Stop'; & " + ' '.join(map(quote,
            [sys.executable, RUNNER, '--log', self.log, '--', sys.executable, self.child, '0']))
        script += '; exit $LASTEXITCODE'
        result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', script],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('stderr retained', self.log.read_text())


if __name__ == '__main__':
    unittest.main()
