"""Check a linked Main candidate against its pinned release and resolve its ARM libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def describe(readelf, path, log):
    text = subprocess.check_output([str(readelf), '-W', '-h', '-l', '-d', '-V', '-A', str(path)], text=True)
    log.write_text(text)
    def value(label):
        found = re.search(r'^\s*' + re.escape(label) + r':\s*(.*?)\s*$', text, re.M)
        if not found:
            raise RuntimeError(f'Missing ELF {label}: {path}')
        return found.group(1)
    interpreter = re.search(r'Requesting program interpreter: ([^\]]+)\]', text)
    needs = text.split("Version needs section '.gnu.version_r'", 1)[1].split('Attribute Section:', 1)[0]
    versions = {}
    library = None
    for line in needs.splitlines():
        match = re.search(r'File: (\S+)', line)
        if match:
            library = match.group(1)
            versions[library] = []
        match = re.search(r'Name: (\S+)', line)
        if match:
            if not library:
                raise RuntimeError('Unbound ELF version requirement')
            versions[library].append(match.group(1))
    return {
        'sha256': digest(path), 'class': value('Class'), 'data': value('Data'),
        'machine': value('Machine'), 'flags': value('Flags'), 'type': value('Type'),
        'interpreter': interpreter.group(1) if interpreter else None,
        'needed': sorted(re.findall(r'\(NEEDED\).*?Shared library: \[([^\]]+)\]', text)),
        'versions': {library: sorted(names) for library, names in sorted(versions.items())},
        'attributes': dict(re.findall(r'^\s*(Tag_\w+):\s*(.*?)\s*$', text, re.M)),
    }

def compile_control(build_dir, source, stage, compiler):
    """Reuse unchanged objects and restore every changed Main translation unit."""
    control = build_dir / 'unpatched-control'
    control.mkdir(exist_ok=True)
    lines = (build_dir / 'build.log').read_text().splitlines()
    link_lines = [line for line in lines if line.startswith('arm-none-linux-gnueabihf-gcc -o bin/MiSTer ')]
    if len(link_lines) != 1:
        raise RuntimeError('Missing unique complete Main link command')
    link_command = shlex.split(link_lines[0])
    link_command[0] = str(compiler)
    link_command[link_command.index('-o') + 1] = str(control / 'MiSTer')
    changed = sorted(name for name, sha in stage['original_sources'].items()
                     if name.endswith(('.cpp', '.c')) and stage['staged_sources'].get(name) != sha)
    if not changed:
        raise RuntimeError('No changed Main translation units to qualify')
    compile_commands = []
    source_hashes = {}
    for name in changed:
        original = subprocess.check_output(['git', '-C', str(Path(__file__).resolve().parents[1] / 'reference/main'),
                                            'show', stage['main_commit'] + ':' + name])
        path = control / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(original)
        if digest(path) != stage['original_sources'][name]:
            raise RuntimeError('Unpatched control does not match the archived Main pin: ' + name)
        object_name = 'bin/' + name + '.o'
        compile_lines = [line.split(' 2>&1 | ', 1)[0] for line in lines
                         if line.startswith('arm-none-linux-gnueabihf-gcc ') and
                         ' -o ' + object_name + ' -c ' + name in line]
        if len(compile_lines) != 1:
            raise RuntimeError('Missing unique Main compile command: ' + name)
        command = shlex.split(compile_lines[0])
        command[0] = str(compiler)
        command[command.index('-o') + 1] = str(path) + '.o'
        command[-1] = str(path)
        link_command[link_command.index(object_name)] = str(path) + '.o'
        compile_commands.append(command)
        source_hashes[name] = digest(path)
    with (control / 'build.log').open('w') as log:
        for command in (*compile_commands, link_command):
            log.write(json.dumps(command) + '\n'); log.flush()
            subprocess.run(command, cwd=source, stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
    record = {'source_sha256': source_hashes, 'main_sha256': digest(control / 'MiSTer'),
              'compile_commands': compile_commands, 'link_command': link_command,
              'build_log_sha256': digest(control / 'build.log')}
    (control / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    return control / 'MiSTer'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    args = parser.parse_args()
    args.build = args.build.resolve()
    args.toolchain = args.toolchain.resolve()
    source = args.build / 'source'
    build = json.loads((args.build / 'build.json').read_text())
    stage = json.loads((args.build / 'stage.json').read_text())
    if build['returncode'] != 0 or digest(args.build / 'stage.json') != build['stage_sha256']:
        raise RuntimeError('Main build did not pass or its staging record changed')
    for name, sha in stage['staged_sources'].items():
        if digest(source / name) != sha:
            raise RuntimeError('Changed source since build: ' + name)
    for name, sha in build['outputs'].items():
        if digest(source / 'bin' / name) != sha:
            raise RuntimeError('Changed Main candidate: ' + name)
    readelf = args.toolchain / 'bin/arm-none-linux-gnueabihf-readelf'
    candidates = sorted(path for path in (source / 'releases').iterdir() if re.fullmatch(r'MiSTer_\d{8}', path.name))
    if not candidates:
        raise RuntimeError('No pinned release available for ABI comparison')
    baseline = candidates[-1]
    actual = describe(readelf, source / 'bin/MiSTer', args.build / 'candidate-abi.log')
    released = describe(readelf, baseline, args.build / 'release-abi.log')
    compiler = args.toolchain / 'bin/arm-none-linux-gnueabihf-gcc'
    control = describe(readelf, compile_control(args.build, source, stage, compiler), args.build / 'control-abi.log')
    for field in ('class', 'data', 'machine', 'flags', 'interpreter', 'needed', 'versions', 'attributes'):
        if actual[field] != control[field]:
            raise RuntimeError('TIC-80 Main changes alter the compiled pin ABI: ' + field)
    for field in ('class', 'data', 'machine', 'flags', 'interpreter'):
        if actual[field] != released[field]:
            raise RuntimeError(f'Changed release ABI field {field}: {actual[field]} vs {released[field]}')
    for field in ('Tag_CPU_arch', 'Tag_CPU_arch_profile', 'Tag_FP_arch', 'Tag_ABI_VFP_args'):
        if actual['attributes'].get(field) != released['attributes'].get(field):
            raise RuntimeError('Changed ARM architecture requirement: ' + field)
    # Resolve the executable in dynamic-loader trace mode. This returns before
    # Main's entry point or hardware initialization is invoked.
    sysroot = Path(subprocess.check_output([str(compiler), '-print-sysroot'], text=True).strip()).resolve()
    libraries = args.build / 'loader-libraries'
    libraries.mkdir(exist_ok=True)
    staged_libraries = {}
    providers = {}
    for directory in ('lib/imlib2', 'lib/bluetooth'):
        for path in sorted((source / directory).glob('*.so')):
            text = subprocess.check_output([str(readelf), '-W', '-d', str(path)], text=True)
            soname = re.search(r'\(SONAME\).*?Library soname: \[([^\]]+)\]', text)
            if not soname or '/' in soname.group(1):
                raise RuntimeError('Missing or invalid library SONAME: ' + str(path))
            destination = libraries / soname.group(1)
            shutil.copyfile(path, destination)
            staged_libraries[destination.name] = digest(destination)
            providers[destination.name] = destination
    # The published executable omits a direct libdl entry, while its pinned
    # Imlib library already requires libdl. Compare the actual dependency graph.
    closure = set(released['needed'])
    required_versions = {lib: set(names) for lib, names in released['versions'].items()}
    inspected = set()
    while closure - inspected:
        library = sorted(closure - inspected)[0]
        inspected.add(library)
        if library in providers:
            descriptor = describe(readelf, providers[library], args.build / (library + '-abi.log'))
            closure.update(descriptor['needed'])
            for lib, names in descriptor['versions'].items():
                required_versions.setdefault(lib, set()).update(names)
    if set(actual['needed']) - closure:
        raise RuntimeError('Candidate adds a library outside the pinned release dependency graph')
    new_release_versions = {}
    raised_release_floors = {}
    for library, versions in actual['versions'].items():
        new = set(versions) - required_versions.get(library, set())
        if new:
            new_release_versions[library] = sorted(new)
        # A newly referenced lower version (for example GLIBCXX_3.4.18
        # alongside an existing 3.4.21 requirement) is not a higher ABI floor.
        for prefix in ('GLIBC_', 'GLIBCXX_', 'CXXABI_', 'GCC_'):
            def highest(names):
                candidates = [name for name in names if name.startswith(prefix) and re.fullmatch(r'[\d.]+', name[len(prefix):])]
                return max(candidates, key=lambda name: tuple(map(int, name[len(prefix):].split('.')))) if candidates else None
            before = highest(required_versions.get(library, ()))
            after = highest(versions)
            if after and (not before or tuple(map(int, after[len(prefix):].split('.'))) > tuple(map(int, before[len(prefix):].split('.')))):
                raised_release_floors.setdefault(library, {})[prefix.rstrip('_')] = {'baseline': before, 'candidate': after}
    search = ':'.join(str(path) for path in (libraries, sysroot / 'lib', sysroot / 'usr/lib'))
    command = ['qemu-arm', '-L', str(sysroot), '-E', 'LD_TRACE_LOADED_OBJECTS=1',
               '-E', 'LD_LIBRARY_PATH=' + search, str(source / 'bin/MiSTer')]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    (args.build / 'loader-list.log').write_text(result.stdout)
    if result.returncode or 'not found' in result.stdout or 'libImlib2.so.1' not in result.stdout or 'libbluetooth.so.3' not in result.stdout:
        raise RuntimeError('ARM dynamic-library resolution failed; see loader-list.log')
    record = {
        'build_record_sha256': digest(args.build / 'build.json'), 'candidate': actual,
        'baseline_path': str(baseline.relative_to(source)), 'baseline': released,
        'unpatched_control': control, 'compiled_pin_abi_unchanged': True,
        'release_dependency_closure': sorted(closure),
        'additional_direct_dependencies': sorted(set(actual['needed']) - set(released['needed'])),
        'release_abi_comparison_passed': not new_release_versions,
        'new_release_symbol_versions': new_release_versions, 'arm_library_resolution_passed': True,
        'raised_release_version_floors': raised_release_floors,
        'staged_libraries': staged_libraries, 'loader_command': command,
        'loader_list_sha256': digest(args.build / 'loader-list.log'),
        'native_target_abi_qualified': False, 'native_functional_qualified': False,
        'device_changes': False,
        'limitation': 'Compared with the pinned released Main and resolved using the compiler sysroot plus pinned MiSTer libraries; native device libraries and hardware behavior remain unverified',
    }
    (args.build / 'qualification.json').write_text(json.dumps(record, indent=2) + '\n')
    print('TIC-80 Main changes preserve the architecture/library/symbol-version ABI of the compiled unpatched pin')
    print(f'Linked Main matches {baseline.name} architecture and dependency graph')
    if new_release_versions:
        print('Symbol-version requirements absent from the release imports: ' + json.dumps(new_release_versions))
        print('Raised version floors: ' + json.dumps(raised_release_floors))
        print('These requirements also occur in the unpatched control; native providers must be checked before use')
    else:
        print('No new symbol-version requirements beyond the published release')
    print('ARM loader resolves all required libraries; Main hardware entry point was not invoked')

if __name__ == '__main__':
    main()
