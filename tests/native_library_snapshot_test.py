"""Exercise provider provenance and interruption without any network access."""
import hashlib
import io
import json
from pathlib import Path
import runpy
import stat
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import hardware_access
import snapshot_native_libraries as subject

HOST = '192.168.1.176'
IMAGE = b'\x7fELF\x01\x01' + b'\0' * 100


class SFTP:
    def __init__(self):
        self.links = {'/lib/libc.so.6': 'libc-2.31.so', '/lib/libasound.so.2': '/usr/lib/libasound.so.2'}
        self.files = {'/lib/libc-2.31.so': IMAGE, '/usr/lib/libasound.so.2': IMAGE + b'ALSA',
                      '/tmp/CORENAME': b'MENU\n', '/proc/version': b'Linux test\n'}
        self.calls = []
        self.on_read = None

    def __enter__(self): return self
    def __exit__(self, *args): pass
    def get_channel(self): return self
    def settimeout(self, seconds):
        assert seconds == 15
        self.calls.append(('timeout', seconds))

    def lstat(self, path):
        self.calls.append(('lstat', path))
        if path in self.links:
            return SimpleNamespace(st_mode=stat.S_IFLNK | 0o777)
        return self.stat(path)

    def readlink(self, path):
        self.calls.append(('readlink', path))
        return self.links[path]

    def stat(self, path):
        self.calls.append(('stat', path))
        if path not in self.files:
            raise FileNotFoundError(path)
        return SimpleNamespace(st_mode=stat.S_IFREG | 0o644, st_size=len(self.files[path]), st_mtime=100)

    def open(self, path, mode):
        self.calls.append(('open', path, mode))
        assert mode == 'rb', 'Remote write attempted'
        if path not in self.files:
            raise FileNotFoundError(path)
        stream = io.BytesIO(self.files[path])
        if self.on_read:
            callback = self.on_read
            original = stream.read
            def read(size):
                data = original(size)
                callback(path)
                return data
            stream.read = read
        return stream


class Client:
    def __init__(self):
        self.sftp = SFTP()
        self.opens = 0
    def open_sftp(self):
        self.opens += 1
        return self.sftp


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.gate = self.root / 'hold.json'
        self.hold(False)
        self.patcher = patch.object(hardware_access, 'HOLD_FILE', self.gate)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)
        self.client = Client()
        self.output = self.root / 'snapshot'

    def hold(self, active):
        self.gate.write_text(json.dumps(dict(active=active, host=HOST, instruction='User is unavailable')))

    def test_provenance_and_no_remote_writes(self):
        result = subject.snapshot(self.client, HOST, self.output, ('libc.so.6', 'libasound.so.2'))
        self.assertTrue(result['complete'])
        self.assertEqual(result['providers']['libc.so.6']['resolved'], '/lib/libc-2.31.so')
        self.assertEqual(result['providers']['libasound.so.2']['resolved'], '/usr/lib/libasound.so.2')
        self.assertEqual(result['providers']['libc.so.6']['symlinks'], [dict(path='/lib/libc.so.6', target='libc-2.31.so')])
        for name, data in [('libc.so.6', IMAGE), ('libasound.so.2', IMAGE + b'ALSA')]:
            self.assertEqual((self.output / 'providers' / name).read_bytes(), data)
            self.assertEqual(result['providers'][name]['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual((self.output / result['metadata']['/tmp/CORENAME']['file']).read_bytes(), b'MENU\n')
        self.assertTrue(result['metadata']['/proc/asound/cards']['missing'])
        self.assertFalse(result['candidate_executed'])
        self.assertFalse(result['native_qualified'])
        self.assertEqual(result['remote_writes'], 0)

    def test_held_before_sftp_and_before_cli_credentials(self):
        self.hold(True)
        with self.assertRaisesRegex(RuntimeError, 'User is unavailable'):
            subject.snapshot(self.client, HOST, self.output)
        self.assertEqual(self.client.opens, 0)
        self.assertFalse(self.output.exists())
        with patch.object(sys, 'argv', ['snapshot_native_libraries.py', '--output', str(self.output)]), \
                patch.dict(sys.modules, paramiko=SimpleNamespace()), patch.dict('os.environ', {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, 'User is unavailable'):
                runpy.run_path(str(ROOT / 'tools/snapshot_native_libraries.py'), run_name='__main__')

    def test_hold_during_read_stops_without_marking_complete(self):
        self.client.sftp.on_read = lambda path: self.hold(True)
        with self.assertRaisesRegex(RuntimeError, 'User is unavailable'):
            subject.snapshot(self.client, HOST, self.output, ('libc.so.6',))
        result = json.loads((self.output / 'snapshot.json').read_text())
        self.assertFalse(result['complete'])
        self.assertEqual(result['providers'], {})
        self.assertEqual(len([row for row in self.client.sftp.calls if row[0] == 'open']), 1)

    def test_replacement_with_same_size_and_timestamp_is_rejected(self):
        def replace(path):
            self.client.sftp.files[path] = IMAGE[:-1] + b'X'
        self.client.sftp.on_read = replace
        with self.assertRaisesRegex(ValueError, 'content changed'):
            subject.snapshot(self.client, HOST, self.output, ('libc.so.6',))
        self.assertFalse((self.output / 'providers/libc.so.6').exists())
        self.assertFalse(json.loads((self.output / 'snapshot.json').read_text())['complete'])

    def test_missing_and_cycle_are_not_successful_snapshots(self):
        result = subject.snapshot(self.client, HOST, self.output, ('libmissing.so.1',))
        self.assertFalse(result['complete'])
        self.assertEqual(result['missing'], ['libmissing.so.1'])
        self.client.sftp.links['/lib/libc-2.31.so'] = 'libc.so.6'
        with self.assertRaisesRegex(ValueError, 'cycle'):
            subject.snapshot(self.client, HOST, self.root / 'cycle', ('libc.so.6',))

    def test_alias_retargeting_during_copy_is_rejected(self):
        self.client.sftp.files['/lib/libc-other.so'] = IMAGE
        def retarget(path):
            self.client.sftp.links['/lib/libc.so.6'] = 'libc-other.so'
        self.client.sftp.on_read = retarget
        with self.assertRaisesRegex(ValueError, 'symlink changed'):
            subject.snapshot(self.client, HOST, self.output, ('libc.so.6',))
        self.assertFalse((self.output / 'providers/libc.so.6').exists())

    def test_oversize_and_nonregular_providers_are_rejected(self):
        with patch.object(subject, 'MAX_LIBRARY_BYTES', len(IMAGE) - 1):
            with self.assertRaisesRegex(ValueError, 'file type or size'):
                subject.snapshot(self.client, HOST, self.output, ('libc.so.6',))
        original = self.client.sftp.lstat
        def fifo(path):
            if path == '/lib/libc-2.31.so':
                return SimpleNamespace(st_mode=stat.S_IFIFO | 0o600)
            return original(path)
        with patch.object(self.client.sftp, 'lstat', side_effect=fifo):
            with self.assertRaisesRegex(ValueError, 'not a regular file'):
                subject.snapshot(self.client, HOST, self.root / 'fifo', ('libc.so.6',))

    def test_transfer_timeout_is_recorded_without_retry(self):
        def stall(path): raise TimeoutError('SFTP transfer deadline')
        self.client.sftp.on_read = stall
        with self.assertRaisesRegex(TimeoutError, 'transfer deadline'):
            subject.snapshot(self.client, HOST, self.output, ('libc.so.6',))
        result = json.loads((self.output / 'snapshot.json').read_text())
        self.assertFalse(result['complete'])
        self.assertEqual(result['error'], 'TimeoutError: SFTP transfer deadline')
        self.assertEqual(len([row for row in self.client.sftp.calls if row[0] == 'open']), 1)

    def test_bad_names_nonelf_and_existing_output_fail(self):
        with self.assertRaisesRegex(ValueError, 'basename'):
            subject.snapshot(self.client, HOST, self.output, ('../libc.so.6',))
        self.assertEqual(self.client.opens, 0)
        self.output.mkdir()
        with self.assertRaises(FileExistsError):
            subject.snapshot(self.client, HOST, self.output, ('libc.so.6',))
        self.assertEqual(self.client.opens, 0)
        self.client.sftp.files['/lib/libc-2.31.so'] = b'not a library'
        with self.assertRaisesRegex(ValueError, 'ELF32'):
            subject.snapshot(self.client, HOST, self.root / 'nonelf', ('libc.so.6',))


if __name__ == '__main__':
    unittest.main()
