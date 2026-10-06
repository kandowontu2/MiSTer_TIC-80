"""Exercise exact payload binding for explicit and legacy qualification receipts."""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
import package_candidate
from candidate_components import DESTINATIONS, LEGACY_CA, LEGACY_PLAYER, qualified_hashes


class CandidatePackage(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='tic80-package-test-')
        self.addCleanup(self.temporary.cleanup)
        self.task = Path(self.temporary.name)
        self.payload = {}
        for name in sorted(DESTINATIONS):
            source = 'frozen/' + name
            path = self.task / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('test-only component: ' + name).encode())
            self.payload[name] = dict(source=source, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                executable=not name.endswith(('.rbf', '.pem')))
        self.receipt = dict(bounded_studio_playback_checks_passed=True, bounded_player_playback_checks_passed=True,
            fresh_hdmi_stereo_confirmation=True, restored_tetris_verified=True,
            main_sha256=self.payload['MiSTer']['sha256'],
            fpga_sha256=self.payload['_Other/TIC80_20261003.rbf']['sha256'],
            studio_sha256=self.payload['games/TIC-80/TIC-80-Studio']['sha256'],
            handler_sha256=self.payload['games/TIC-80/_handler.sh']['sha256'],
            player_sha256=self.payload['games/TIC-80/TIC-80']['sha256'],
            cacert_sha256=self.payload['games/TIC-80/cacert.pem']['sha256'],
            payload=self.payload, evidence_directory='evidence', files={}, open_gates=['unit-test fixture only'])
        for name in ('LICENSE', 'NOTICE', 'assets/LICENSE.cacert',
                     'build/sdk-native-candidate/licenses/runtime.txt', 'evidence/receipt.txt'):
            path = self.task / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('unit-test fixture only\n')
        proof = self.task / 'evidence/receipt.txt'
        self.receipt['files']['receipt.txt'] = dict(sha256=hashlib.sha256(proof.read_bytes()).hexdigest())
        self.destination = self.task / 'output'

    def package(self):
        receipt = self.task / 'qualification.json'
        receipt.write_text(json.dumps(self.receipt))
        with patch.object(package_candidate, 'ROOT', self.task):
            return package_candidate.package(self.destination, receipt)

    def test_explicit_components_copy_exact_qualified_sources(self):
        result = self.package()
        self.assertEqual(set(result['files']), DESTINATIONS)
        self.assertFalse(result['release_accepted'])
        self.assertFalse(result['goal_complete'])
        for name, expected in self.payload.items():
            self.assertEqual(result['files'][name]['sha256'], expected['sha256'])
            self.assertEqual((self.destination / 'sd-card' / name).read_bytes(),
                             (self.task / expected['source']).read_bytes())

    def test_changed_player_is_rejected_before_output_creation(self):
        (self.task / self.payload['games/TIC-80/TIC-80']['source']).write_bytes(b'changed player')
        with self.assertRaises(AssertionError): self.package()
        self.assertFalse(self.destination.exists())

    def test_rehashed_CA_mapping_cannot_replace_qualified_CA(self):
        item = self.payload['games/TIC-80/cacert.pem']
        path = self.task / item['source']; path.write_bytes(b'changed CA')
        item['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(AssertionError, 'Unqualified mapped component'): self.package()
        self.assertFalse(self.destination.exists())

    def test_partial_explicit_qualification_is_rejected(self):
        del self.receipt['cacert_sha256']
        with self.assertRaisesRegex(AssertionError, 'supplied together'): self.package()
        self.assertFalse(self.destination.exists())

    def test_matching_player_hash_requires_player_playback_evidence(self):
        del self.receipt['bounded_player_playback_checks_passed']
        with self.assertRaisesRegex(AssertionError, 'own playback qualification'): self.package()
        self.assertFalse(self.destination.exists())

    def test_mapped_sources_cannot_escape_workspace(self):
        self.payload['MiSTer']['source'] = '../elsewhere'
        with self.assertRaisesRegex(AssertionError, 'Invalid source path'): self.package()
        self.assertFalse(self.destination.exists())

    def test_legacy_receipts_retain_exact_player_and_CA_pins(self):
        del self.receipt['player_sha256']; del self.receipt['cacert_sha256']
        hashes = qualified_hashes(self.receipt)
        self.assertEqual(hashes['games/TIC-80/TIC-80'], LEGACY_PLAYER)
        self.assertEqual(hashes['games/TIC-80/cacert.pem'], LEGACY_CA)


if __name__ == '__main__': unittest.main()
