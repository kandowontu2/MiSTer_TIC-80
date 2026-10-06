"""Build the pinned Main candidate locally, without deployment scripts or device I/O."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
PIN = '5a3a08662c25bd792043f8a8fb48e4be12099beb'

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')

def output(command, **options):
    return subprocess.check_output(command, text=True, **options).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'reference/main')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--date', default=datetime.now(timezone.utc).strftime('%y%m%d'))
    parser.add_argument('--stage-only', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8 or not re.fullmatch(r'\d{6}', args.date):
        parser.error('jobs must be 1..8 and date must be YYMMDD')
    args.source = args.source.resolve()
    args.output = args.output.resolve()
    # Never turn a reference checkout into the staging/output directory.
    if args.output == args.source or args.output in args.source.parents or args.source in args.output.parents:
        parser.error('output and reference checkout must be separate trees')
    if output(['git', '-C', str(args.source), 'rev-parse', 'HEAD']) != PIN:
        raise RuntimeError('Main reference does not match the qualified pin')
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.output / 'source'
    if source.exists():
        raise RuntimeError('Use a fresh output directory; existing builds are retained as evidence')
    source.mkdir()
    archive = subprocess.Popen(['git', '-C', str(args.source), 'archive', '--format=tar', PIN], stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=archive.stdout, mode='r|') as stream:
            stream.extractall(source, filter='data')
    finally:
        archive.stdout.close()
    if archive.wait():
        raise RuntimeError('Pinned Main archive extraction failed')
    original_hashes = {str(path.relative_to(source)): digest(path) for path in sorted(source.rglob('*')) if path.is_file()}
    subprocess.run([sys.executable, str(ROOT / 'tools/stage_main_source.py'), '--source', str(source), '--output', str(source)], check=True)
    makefile = source / 'Makefile'
    make = makefile.read_text()
    parallel = 'MAKEFLAGS += "-j $(shell nproc)"'
    date = '-DVDATE=\\"`date +"%y%m%d"`\\"'
    if make.count(parallel) != 1 or make.count(date) != 1:
        raise RuntimeError('Pinned Main job/date bindings changed')
    make = make.replace(parallel, '# Parallelism is selected by the local build wrapper.')
    make = make.replace(date, '-DVDATE=\\"' + args.date + '\\"')
    makefile.write_text(make)
    staged_hashes = {str(path.relative_to(source)): digest(path) for path in sorted(source.rglob('*')) if path.is_file()}
    stage = {
        'main_commit': PIN, 'version_date': args.date, 'original_sources': original_hashes,
        'staged_sources': staged_hashes, 'wrapper_sha256': digest(Path(__file__)),
        'source_stager_sha256': digest(ROOT / 'tools/stage_main_source.py'),
        'device_changes': False,
    }
    write_json(args.output / 'stage.json', stage)
    print(f'Staged {len(staged_hashes)} files from pinned Main; no deployment commands invoked', flush=True)
    if args.stage_only:
        return
    if not args.toolchain:
        parser.error('--toolchain is required for a build')
    compiler_dir = args.toolchain.resolve() / 'bin'
    compiler = compiler_dir / 'arm-none-linux-gnueabihf-gcc'
    compiler_version = output([str(compiler), '--version'])
    if not re.search(r'\b10\.2\.1\b', compiler_version):
        raise RuntimeError('Expected the pinned GNU Arm 10.2.1 compiler')
    env = os.environ.copy()
    env['PATH'] = str(compiler_dir) + os.pathsep + env['PATH']
    # Respect the explicit -j limit, independent of outer make invocations.
    env.pop('MAKEFLAGS', None)
    env.pop('MFLAGS', None)
    command = ['make', f'-j{args.jobs}', 'V=1']
    print('Building complete Main with pinned GCC 10.2.1', flush=True)
    with (args.output / 'build.log').open('w') as log:
        result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
    record = {
        'recorded_at': datetime.now(timezone.utc).isoformat(), 'main_commit': PIN,
        'stage_sha256': digest(args.output / 'stage.json'), 'command': command,
        'compiler_version': compiler_version, 'compiler_sha256': digest(compiler),
        'returncode': result.returncode, 'build_log_sha256': digest(args.output / 'build.log'),
        'device_changes': False, 'native_qualified': False,
    }
    if result.returncode == 0:
        record['outputs'] = {name: digest(source / 'bin' / name) for name in ('MiSTer', 'MiSTer.elf')}
    write_json(args.output / 'build.json', record)
    if result.returncode:
        raise SystemExit(f'Main build failed ({result.returncode}); see {args.output / "build.log"}')
    print('Linked complete Main candidate; native qualification remains open', flush=True)

if __name__ == '__main__':
    main()
