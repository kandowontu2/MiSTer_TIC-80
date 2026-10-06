"""Run the real Bash installer against disposable SD-card trees."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "Scripts/Install_TIC80.sh"
FILES = ["_Other/TIC80_20261003.rbf", "games/TIC-80/TIC-80",
         "games/TIC-80/TIC-80-Studio", "games/TIC-80/_handler.sh", "games/TIC-80/cacert.pem"]


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tic80 installer ")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "sd card"
        self.bundle = self.base / "bundle"
        self.root.mkdir(); self.bundle.mkdir()
        self.keep = {"MiSTer": b"stock Main sentinel", "MiSTer.ini": b"display settings",
                     "games/TIC-80/Carts/test.tic": b"user cart",
                     "saves/TIC-80/test.pmem": b"user save",
                     "config/TIC-80.map": b"controller mapping",
                     "games/TIC-80/frontend.txt": b"player\n"}
        for path, data in self.keep.items():
            self.put(self.root / path, data)
        for path in FILES:
            data = b"#!/bin/sh\nexit 0\n" if path.endswith(".sh") else b"new payload " + path.encode()
            self.put(self.bundle / "payload" / path, data)
        self.put(self.bundle / "frontier/Master_Daemon.sh", b"#!/bin/bash\nexit 0\n")
        self.manifest()

    @staticmethod
    def put(path, data):
        path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)

    def manifest(self):
        data = "".join(f"{hashlib.sha256((self.bundle / 'payload' / p).read_bytes()).hexdigest()}  {p}\n" for p in FILES)
        (self.bundle / "files.sha256").write_text(data)
        daemon = self.bundle / "frontier/Master_Daemon.sh"
        (self.bundle / "frontier.sha256").write_text(f"{hashlib.sha256(daemon.read_bytes()).hexdigest()}  Master_Daemon.sh\n")

    def run_script(self, *args, ok=True, env=None):
        result = subprocess.run(["bash", str(SCRIPT), "--root", str(self.root),
                                 "--bundle", str(self.bundle), *args],
                                capture_output=True, text=True, timeout=15, env=env)
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        for path, data in self.keep.items():
            self.assertEqual((self.root / path).read_bytes(), data, path)
        self.assertFalse((self.root / "Scripts/.tic80-install.lock").exists())
        return result

    def test_install_and_repeat(self):
        self.run_script()
        startup = (self.root / "linux/user-startup.sh").read_bytes()
        for path in FILES:
            self.assertEqual((self.root / path).read_bytes(), (self.bundle / "payload" / path).read_bytes())
        self.assertTrue(os.access(self.root / "games/TIC-80/TIC-80-Studio", os.X_OK))
        self.run_script()
        self.assertEqual((self.root / "linux/user-startup.sh").read_bytes(), startup)
        self.assertEqual(startup.count(b"bash /media/fat/MiSTer_Frontier/Master_Daemon.sh &"), 1)

    def test_existing_frontier_and_startup(self):
        daemon = b"#!/bin/bash\n# user's shared Frontier\nexit 0\n"
        self.put(self.root / "MiSTer_Frontier/Master_Daemon.sh", daemon)
        startup = b"#!/bin/bash\necho other service\nbash /media/fat/MiSTer_Frontier/Master_Daemon.sh &\nexit 0\n"
        self.put(self.root / "linux/user-startup.sh", startup)
        self.run_script()
        self.assertEqual((self.root / "MiSTer_Frontier/Master_Daemon.sh").read_bytes(), daemon)
        self.assertEqual((self.root / "linux/user-startup.sh").read_bytes(), startup)

    def test_disabled_startup_with_early_exit(self):
        startup = b"#!/bin/sh\n# Master_Daemon.sh is only a comment\nexit 0\n"
        self.put(self.root / "linux/_user-startup.sh", startup)
        self.run_script()
        result = (self.root / "linux/user-startup.sh").read_bytes()
        self.assertLess(result.index(b"bash /media"), result.index(b"exit 0"))
        self.assertEqual((self.root / "linux/_user-startup.sh").read_bytes(), startup)

    def test_check_is_read_only(self):
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        self.run_script("--check")
        after = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_corrupt_bundle_rejected_before_writes(self):
        (self.bundle / "payload" / FILES[1]).write_bytes(b"corrupted")
        self.run_script(ok=False)
        self.assertFalse((self.root / "_Other").exists())

    def test_forbidden_main_payload_rejected(self):
        with (self.bundle / "files.sha256").open("a") as f:
            f.write("0" * 64 + "  MiSTer\n")
        self.run_script(ok=False)
        self.assertFalse((self.root / "_Other").exists())

    def test_symlink_destination_rejected(self):
        (self.root / "games/TIC-80/TIC-80").symlink_to(self.root / "MiSTer")
        self.run_script(ok=False)
        self.assertFalse((self.root / "_Other").exists())

    def test_partial_update_rolls_back(self):
        originals = {p: b"old payload " + p.encode() for p in FILES}
        for p, data in originals.items():
            self.put(self.root / p, data)
        # Fail an actual atomic promotion after earlier files have changed.
        mock = self.base / "commands/mv"
        self.put(mock, b'#!/bin/bash\ncase "${@: -1}" in */games/TIC-80/TIC-80-Studio) case "$1 $2" in *tic80-new*) exit 1;; esac;; esac\nexec /bin/mv "$@"\n')
        mock.chmod(0o755)
        self.run_script(ok=False, env=dict(os.environ, PATH=str(mock.parent) + ":" + os.environ["PATH"]))
        for p, data in originals.items():
            self.assertEqual((self.root / p).read_bytes(), data)
        self.assertFalse((self.root / "linux/user-startup.sh").exists())

    def test_rollback_retains_shared_frontier(self):
        old = b"old runtime"
        self.put(self.root / FILES[1], old)
        self.run_script()
        backup, = [p for p in (self.root / "Scripts/TIC80-backups").iterdir() if (p / "complete").exists()]
        self.run_script("--rollback", str(backup))
        self.assertEqual((self.root / FILES[1]).read_bytes(), old)
        self.assertFalse((self.root / FILES[0]).exists())
        self.assertTrue((self.root / "MiSTer_Frontier/Master_Daemon.sh").exists())

    def test_rollback_refuses_later_changes(self):
        self.run_script()
        backup, = (self.root / "Scripts/TIC80-backups").iterdir()
        (self.root / FILES[2]).write_bytes(b"later user version")
        self.run_script("--rollback", str(backup), ok=False)
        self.assertTrue((self.root / FILES[0]).exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
