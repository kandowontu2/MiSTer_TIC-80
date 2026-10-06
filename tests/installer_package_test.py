"""The distribution must omit Main even when its source prototype includes it."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_installer", ROOT / "tools/package_installer.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.input = self.base / "prototype"
        paths = ("MiSTer", "_Other/TIC80_20261003.rbf", *packager.RUNTIME_PATHS)
        manifest = {"files": {}}
        for path in paths:
            source = self.input / "sd-card" / path
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(b"payload: " + path.encode())
            manifest["files"][path] = {"sha256": packager.digest(source)}
        (self.input / "manifest.json").write_text(json.dumps(manifest))
        (self.input / "licenses").mkdir()
        (self.input / "licenses/LICENSE").write_text("fixture license")

    def test_main_omitted_and_qualification_not_inherited(self):
        output = self.base / "installer.v0.1.0-dev.20261005"
        packager.build(self.input, output)
        with zipfile.ZipFile(Path(str(output) + ".zip")) as z:
            self.assertIsNone(z.testzip())
            self.assertNotIn("MiSTer", z.namelist())
            self.assertFalse(any(p.endswith("/MiSTer") for p in z.namelist()))
            status = json.loads(z.read("manifest.json"))
            self.assertFalse(status["stock_main_hardware_qualified"])
            self.assertFalse(status["release_accepted"])
            for path, expected in status["files"].items():
                self.assertEqual(hashlib.sha256(z.read(path)).hexdigest(), expected)
        with self.assertRaises(FileExistsError):
            packager.build(self.input, output)

    def test_corrupt_input_rejected_without_output(self):
        (self.input / "sd-card/games/TIC-80/TIC-80").write_bytes(b"bad")
        with self.assertRaises(ValueError):
            packager.build(self.input, self.base / "installer")
        self.assertFalse((self.base / "installer").exists())

    def test_manifest_path_traversal_rejected(self):
        manifest = json.loads((self.input / "manifest.json").read_text())
        manifest["files"]["_Other/TIC80_../../MiSTer.rbf"] = manifest["files"].pop("_Other/TIC80_20261003.rbf")
        (self.input / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaises(ValueError):
            packager.build(self.input, self.base / "installer")

    def test_evidence_is_bound_to_payload_and_does_not_confer_acceptance(self):
        manifest_path = self.input / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        hashes = {name: item["sha256"] for name, item in manifest["files"].items() if name != "MiSTer"}
        qualification = self.input / "qualification.json"
        qualification.write_text(json.dumps({"payload_sha256": hashes, "full_release_qualified": False}))
        manifest["qualification_sha256"] = packager.digest(qualification)
        manifest_path.write_text(json.dumps(manifest))
        output = self.base / "with-evidence"
        packager.build(self.input, output)
        with zipfile.ZipFile(str(output) + ".zip") as z:
            self.assertEqual(z.read("docs/TIC-80/qualification.json"), qualification.read_bytes())
            status = json.loads(z.read("manifest.json"))
            self.assertTrue(status["bounded_runtime_evidence_included"])
            self.assertFalse(status["release_accepted"])
            self.assertFalse(status["stock_main_hardware_qualified"])
        qualification.write_text(json.dumps({"payload_sha256": {}, "full_release_qualified": False}))
        with self.assertRaisesRegex(ValueError, "checksum"):
            packager.build(self.input, self.base / "bad-evidence")
        manifest["qualification_sha256"] = packager.digest(qualification)
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "different payloads"):
            packager.build(self.input, self.base / "mismatched-evidence")
        self.assertFalse((self.base / "bad-evidence").exists())
        self.assertFalse((self.base / "mismatched-evidence").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
