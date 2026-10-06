"""Copy MiSTer library providers for offline ABI inspection, without running candidates."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import stat

from hardware_access import require_access
from hardware_ssh import connect

LIBRARIES = ('ld-linux-armhf.so.3', 'libc.so.6', 'libpthread.so.0',
             'libdl.so.2', 'libm.so.6', 'librt.so.1', 'libasound.so.2')
ROOTS = ('/lib', '/usr/lib')
MAX_LIBRARY_BYTES = 16 * 1024 * 1024


def resolve(sftp, host, path):
    """Retain the actual symlink chain and reject cycles or nonregular providers."""
    chain = []
    for _ in range(17):
        require_access(host)
        if path in [row['path'] for row in chain]:
            raise ValueError('Library symlink cycle: ' + path)
        info = sftp.lstat(path)
        if stat.S_ISREG(info.st_mode):
            return path, chain
        if not stat.S_ISLNK(info.st_mode):
            raise ValueError('Provider is not a regular file: ' + path)
        if len(chain) == 16:
            raise ValueError('Library symlink chain exceeds 16 links')
        require_access(host)
        target = sftp.readlink(path)
        chain.append(dict(path=path, target=target))
        path = posixpath.normpath(posixpath.join(posixpath.dirname(path), target))
    raise ValueError('Library symlink chain exceeds 16 links')


def read_stable(sftp, host, path, maximum):
    require_access(host)
    before = sftp.stat(path)
    if not stat.S_ISREG(before.st_mode) or not 0 <= before.st_size <= maximum:
        raise ValueError('Unexpected file type or size: ' + path)
    def read():
        content = bytearray()
        require_access(host)
        with sftp.open(path, 'rb') as remote:
            while True:
                require_access(host)
                chunk = remote.read(min(65536, maximum - len(content) + 1))
                if not chunk:
                    break
                content.extend(chunk)
                if len(content) > maximum:
                    raise ValueError('File grew beyond snapshot limit: ' + path)
        return bytes(content)
    content = read()
    # A replacement can preserve size and timestamp. A second independent
    # read must match byte for byte before retaining a provider fingerprint.
    if read() != content:
        raise ValueError('File content changed during snapshot: ' + path)
    require_access(host)
    after = sftp.stat(path)
    if (before.st_size, before.st_mtime) != (after.st_size, after.st_mtime) or len(content) != after.st_size:
        raise ValueError('File changed during snapshot: ' + path)
    return bytes(content)


def snapshot(client, host, output, libraries=LIBRARIES, roots=ROOTS):
    require_access(host)
    if not libraries or len(libraries) > 64:
        raise ValueError('Select between one and 64 libraries')
    if len(set(libraries)) != len(libraries):
        raise ValueError('Duplicate library name')
    for name in libraries:
        if not re.fullmatch(r'[A-Za-z0-9_+.-]+\.so(?:\.[A-Za-z0-9_+.-]+)?', name):
            raise ValueError('Expected a library basename: ' + name)
    if not roots or any(not path.startswith('/') or posixpath.normpath(path) != path for path in roots):
        raise ValueError('Library roots must be normalized absolute paths')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    providers = output / 'providers'
    providers.mkdir()
    record = dict(host=host, recorded_at=datetime.now(timezone.utc).isoformat(),
                  complete=False, candidate_executed=False, native_qualified=False,
                  remote_writes=0, roots=list(roots), providers={}, missing=[], metadata={})
    try:
        require_access(host)
        with client.open_sftp() as sftp:
            sftp.get_channel().settimeout(15)
            for name in libraries:
                alias = None
                for folder in roots:
                    candidate = posixpath.join(folder, name)
                    try:
                        resolved, chain = resolve(sftp, host, candidate)
                    except FileNotFoundError:
                        continue
                    alias = candidate
                    break
                if alias is None:
                    record['missing'].append(name)
                    continue
                data = read_stable(sftp, host, resolved, MAX_LIBRARY_BYTES)
                after_resolved, after_chain = resolve(sftp, host, alias)
                if (after_resolved, after_chain) != (resolved, chain):
                    raise ValueError('Provider symlink changed during snapshot: ' + alias)
                if len(data) < 20 or data[:6] != b'\x7fELF\x01\x01':
                    raise ValueError('Provider is not a little-endian ELF32 image: ' + resolved)
                (providers / name).write_bytes(data)
                record['providers'][name] = dict(alias=alias, resolved=resolved, symlinks=chain,
                    bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            for path in ('/proc/version', '/etc/os-release', '/tmp/CORENAME', '/proc/asound/cards', '/proc/asound/pcm'):
                # procfs reports size zero while reads return data; ordinary
                # provider stability checks intentionally do not apply here.
                require_access(host)
                try:
                    with sftp.open(path, 'rb') as remote:
                        require_access(host)
                        data = remote.read(65537)
                except FileNotFoundError:
                    record['metadata'][path] = dict(missing=True)
                    continue
                if len(data) > 65536:
                    raise ValueError('Metadata exceeds snapshot limit: ' + path)
                local = 'metadata/' + path.lstrip('/')
                destination = output / local
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
                record['metadata'][path] = dict(file=local, sha256=hashlib.sha256(data).hexdigest())
        record['complete'] = not record['missing']
        return record
    except Exception as error:
        record['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        (output / 'snapshot.json').write_text(json.dumps(record, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.1.176')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--library', action='append', help='Explicit provider basename; repeat for extra dependencies')
    args = parser.parse_args()
    require_access(args.host)
    # Hold checking precedes credentials and client creation. This tool never
    # executes remote commands, uploads files, loads cores or writes board files.
    import paramiko
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    keys = Path(__file__).resolve().parents[1] / 'build/ssh_known_hosts'
    if keys.exists():
        client.load_host_keys(str(keys))
    try:
        connect(client, args.host, username='root', password=os.environ['TM_SSH_PASSWORD'], timeout=10)
        record = snapshot(client, args.host, args.output, libraries=args.library or LIBRARIES)
    finally:
        client.close()
    if not record['complete']:
        parser.exit(1, 'Provider snapshot incomplete: ' + ', '.join(record['missing']) + '\n')
    print(f"Copied {len(record['providers'])} providers for offline ABI inspection; no candidate executed")


if __name__ == '__main__':
    main()
