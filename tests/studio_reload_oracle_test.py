"""The execution oracle must reject an intentionally early initialization edge."""
import argparse
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--frontend', required=True)
parser.add_argument('--trace-library', required=True)
args = parser.parse_args()
fixture = Path(__file__).with_name('studio_reload_test.py')
result = subprocess.run([
    sys.executable, str(fixture), '--frontend', args.frontend,
    '--trace-library', args.trace_library, '--scenario', 'queued',
    '--early-initialization-negative-control',
], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=25)
assert result.returncode == 1, result.stdout
assert 'AssertionError: Cartridge TIC ran before Main initialization' in result.stdout, result.stdout
print('Early initialization control rejected by the actual cartridge execution trace')
