"""Run a native command once and stream merged output to a UTF-8 log.

Python owns the native exit status, avoiding PowerShell 5's stderr exception
handling and mixed-encoding redirects. Existing non-append evidence is reserved
before launching the process, so a retry cannot silently duplicate a build.
"""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--append', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('A native command is required after --')
    try:
        if args.append and args.log.exists():
            if '\0' in args.log.read_text(encoding='utf-8'):
                raise ValueError('Existing log contains mixed or non-UTF-8 output')
        with args.log.open('a' if args.append else 'x', encoding='utf-8', newline='\n') as log:
            print('Native command started; log:', args.log, flush=True)
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors='replace') as process:
                for line in process.stdout:
                    log.write(line)
                    log.flush()
                status = process.wait()
        print('Native exit code:', status, flush=True)
        if status:
            print(args.log.read_text(encoding='utf-8')[-2200:])
        return status
    except (OSError, ValueError) as error:
        # Report through stdout so PowerShell checks the actual exit code.
        print('Native command wrapper failed:', str(error), flush=True)
        return 2


if __name__ == '__main__':
    sys.exit(main())
