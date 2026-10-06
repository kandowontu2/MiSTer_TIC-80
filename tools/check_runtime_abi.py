"""Check an ARM runtime and its explicit library closure without executing it.

This establishes compatibility with the supplied providers, not with an
uninspected MiSTer filesystem or physical audio device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inspect(path, readelf):
    def output(*flags):
        return subprocess.check_output([readelf, *flags, str(path)], text=True)
    header, attributes = output('-hW'), output('-AW')
    if not all(value in header for value in ('ELF32', 'little endian', 'Machine:                           ARM', 'hard-float ABI')):
        raise ValueError(f'{path}: expected ARM32 little-endian hard-float ELF')
    architecture = re.search(r'Tag_CPU_arch: ([^\n]+)', attributes)
    if architecture and architecture.group(1).strip() not in ('v4', 'v4T', 'v5T', 'v5TE', 'v5TEJ', 'v6', 'v6K', 'v6T2', 'v7'):
        raise ValueError(f'{path}: CPU requirements exceed the MiSTer ARMv7 target')
    for tag, supported in [('Tag_FP_arch', {'VFPv2', 'VFPv3', 'VFPv3-D16'}),
                           ('Tag_Advanced_SIMD_arch', {'NEONv1'})]:
        attribute = re.search(tag + r': ([^\n]+)', attributes)
        if attribute and attribute.group(1).strip() not in supported:
            raise ValueError(f'{path}: unsupported Cortex-A9 requirement {attribute.group(0)}')
    dynamic, program = output('-dW'), output('-lW')
    needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', dynamic)
    interpreter = re.search(r'Requesting program interpreter: ([^\]]+)', program)
    imports, exports = [], set()
    for line in output('--dyn-syms', '-W').splitlines():
        columns = line.split()
        if len(columns) < 8 or not re.fullmatch(r'\d+:', columns[0]):
            continue
        name = columns[7].replace('@@', '@')
        if columns[6] == 'UND':
            if columns[4] == 'GLOBAL':
                imports.append(name)
        elif columns[4] in ('GLOBAL', 'WEAK') and columns[5] in ('DEFAULT', 'PROTECTED'):
            exports.add(name)
            # An unversioned dlsym can select the default version of a symbol.
            if '@@' in columns[7] or '@' not in columns[7]:
                exports.add(name.split('@', 1)[0])
    return dict(path=str(path), sha256=digest(path), needed=needed,
                interpreter=interpreter.group(1) if interpreter else None,
                imports=imports, exports=sorted(exports), attributes=attributes)


def audit(binary, roots, readelf, dlopen, maximum, miniaudio):
    entry = inspect(binary, readelf)
    if entry['interpreter'] != '/lib/ld-linux-armhf.so.3':
        raise ValueError(f'{binary}: microphone profile needs the dynamic ARM hard-float loader')
    if 'Tag_CPU_arch: v7' not in entry['attributes'] or 'Tag_ABI_VFP_args: VFP registers' not in entry['attributes']:
        raise ValueError(f'{binary}: expected ARMv7 and VFP argument convention')
    forbidden = {'libstdc++.so.6', 'libgcc_s.so.1'}
    providers = {}

    def resolve(name):
        if Path(name).name != name:
            raise ValueError(f'nonportable library dependency: {name}')
        for directory in roots:
            candidate = directory / name
            if candidate.is_file():
                return candidate.resolve()
        raise ValueError(f'missing provider: {name}')

    pending = [*entry['needed'], Path(entry['interpreter']).name, *dlopen]
    while pending:
        name = pending.pop()
        if name in forbidden:
            raise ValueError(f'{binary}: added board compiler-runtime dependency {name}')
        if name in providers:
            continue
        provider = inspect(resolve(name), readelf)
        providers[name] = provider
        pending.extend(provider['needed'])
    available = set(entry['exports'])
    for provider in providers.values():
        available.update(provider['exports'])
    for name, image in [('application', entry), *providers.items()]:
        for symbol in image['imports']:
            version = symbol.split('@', 1)[1] if '@' in symbol else None
            if version and version.startswith('GLIBC_'):
                if version == 'GLIBC_PRIVATE' and name != 'application':
                    pass
                elif not re.fullmatch(r'GLIBC_\d+(?:\.\d+)+', version):
                    raise ValueError(f'{image["path"]}: unsupported libc requirement {version}')
                elif tuple(map(int, version[6:].split('.'))) > maximum:
                    raise ValueError(f'{image["path"]}: libc requirement {version} exceeds configured floor')
            if symbol not in available:
                raise ValueError(f'{image["path"]}: unresolved strong import {symbol}')
    if miniaudio:
        code = miniaudio.read_text()
        start = code.index('static ma_result ma_context_init__alsa(')
        end = code.index('\nstatic ', start + 1)
        required = sorted(set(re.findall(r'ma_dlsym\([^\n]+, "(snd_[^"]+)"\)', code[start:end])))
        if not required or 'libasound.so.2' not in providers:
            raise ValueError('ALSA symbol audit needs its explicit provider and miniaudio initialization body')
        exported = set(providers['libasound.so.2']['exports'])
        missing = sorted(set(required) - exported)
        if missing:
            raise ValueError('ALSA provider lacks miniaudio symbols: ' + ', '.join(missing))
    else:
        required = []
    return dict(application=entry, providers=providers, alsa_required_symbols=required,
                maximum_glibc='.'.join(map(str, maximum)), device_access=False, native_qualified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--library-root', type=Path, action='append', required=True)
    parser.add_argument('--readelf', default='readelf')
    parser.add_argument('--dlopen-library', action='append', default=[])
    parser.add_argument('--maximum-glibc', default='2.31')
    parser.add_argument('--miniaudio', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        maximum = tuple(map(int, args.maximum_glibc.split('.')))
        result = audit(args.binary, args.library_root, args.readelf, args.dlopen_library, maximum, args.miniaudio)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Runtime ABI rejected: {error}\n')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Runtime ABI passed: {args.binary}; {len(result["providers"])} providers, '
          f'{len(result["alsa_required_symbols"])} ALSA symbols, GLIBC <= {args.maximum_glibc}; native verification pending')


if __name__ == '__main__':
    main()
