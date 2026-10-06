"""A corresponding-source archive must bind to exact per-core payloads."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_sources", ROOT / "tools/package_sources.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class IdentityTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.paths = ["_Other/TIC80_20261006.rbf", "games/TIC-80/TIC-80",
                      "games/TIC-80/TIC-80-Studio", "games/TIC-80/_handler.sh",
                      "games/TIC-80/cacert.pem"]
        self.manifest = {"shared_Main_payload_included": False, "files": {}}
        for name in self.paths:
            path = self.root / "candidate" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
            self.manifest["files"][name] = {"candidate_sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        self.path = self.root / "manifest.json"
        self.save()

    def save(self):
        self.path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_exact_core_and_all_payloads_bound(self):
        core, identity = packager.candidate_identity(self.path)
        self.assertEqual(core, self.manifest["files"][self.paths[0]]["candidate_sha256"])
        self.assertEqual(identity["manifest_sha256"], hashlib.sha256(self.path.read_bytes()).hexdigest())
        self.assertEqual(set(identity["files"]), set(self.paths))
        self.assertNotIn("qualified", identity)

    def test_changed_runtime_rejected(self):
        (self.root / "candidate" / self.paths[1]).write_bytes(b"different runtime")
        with self.assertRaises(AssertionError):
            packager.candidate_identity(self.path)

    def test_main_payload_and_path_traversal_rejected(self):
        for extra in ["MiSTer", "../MiSTer", "_Other/TIC80_../../MiSTer.rbf"]:
            with self.subTest(extra=extra):
                self.manifest["files"][extra] = {"candidate_sha256": "0" * 64}
                self.save()
                with self.assertRaises(AssertionError):
                    packager.candidate_identity(self.path)
                del self.manifest["files"][extra]

    def test_shared_main_flag_rejected(self):
        self.manifest["shared_Main_payload_included"] = True
        self.save()
        with self.assertRaises(AssertionError):
            packager.candidate_identity(self.path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
