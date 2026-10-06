"""Provide a private Studio folder; propagate permission skips from the test."""
import subprocess,sys,tempfile
with tempfile.TemporaryDirectory(prefix='tic80-studio-priority-') as folder:
    sys.exit(subprocess.run([sys.argv[1],folder]).returncode)
