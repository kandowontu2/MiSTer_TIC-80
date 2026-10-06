"""Local SSH-boundary regressions: core ownership, rollback and launch ambiguity."""
import io
import json
from pathlib import Path
import shlex
import sys
import tempfile
import types
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
# These tests replace the whole SSH boundary. They do not require an installed
# networking dependency; a missing-module stub fails closed if left unpatched.
try:
    import paramiko
except ModuleNotFoundError as error:
    if error.name != "paramiko":
        raise
    def no_network():
        raise AssertionError("An offline test attempted to create a real SSH client")
    sys.modules["paramiko"] = types.SimpleNamespace(SSHClient=no_network)
import test_hid_pan_native as driver


class RemoteFile(io.BytesIO):
    def __init__(self, board, path, data=b"", writing=False):
        super().__init__(data)
        self.board, self.path, self.writing = board, path, writing

    def close(self):
        if self.writing and not self.closed:
            self.board.files[self.path] = self.getvalue()
        super().close()


class Board:
    def __init__(self, fault=None):
        self.fault, self.core, self.pid = fault, "PICO-8", "101"
        self.commands, self.files, self.launches = [], {}, 0
        self.original = "/media/fat/_Other/PICO8_20251012.rbf"
        paths = ("_Other/TIC80_20261003.rbf", "games/TIC-80/TIC-80", "games/TIC-80/TIC-80-Studio",
                 "games/TIC-80/_handler.sh", "games/TIC-80/cacert.pem")
        self.hashes = {"/media/fat/" + path: driver.hashlib.sha256(path.encode()).hexdigest() for path in paths}

    def load_system_host_keys(self):
        pass

    def close(self):
        pass

    def open_sftp(self):
        return self

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        pass

    def open(self, path, mode):
        if path.startswith("/proc/"):
            return RemoteFile(self, path, ("/media/fat/MiSTer\0" + self.original + "\0").encode())
        return RemoteFile(self, path, self.files.get(path, b""), "w" in mode)

    def put(self, source, destination):
        self.files[destination] = Path(source).read_bytes()

    def run(self, unused_client, command, timeout=45):
        self.commands.append(command)
        if command == "cat /tmp/CORENAME":
            return self.core + "\n"
        if command == "pidof MiSTer":
            return self.pid + "\n"
        if command.startswith("sha256sum"):
            path = shlex.split(command)[-1]
            if path == "/media/fat/MiSTer" or path.startswith("/proc/"):
                digest = "0" * 64 if self.fault == "wrong-main" else driver.STOCK
            elif path in self.files:
                digest = driver.hashlib.sha256(self.files[path]).hexdigest()
            else:
                digest = self.hashes.get(path, "a" * 64)
            return digest + "  " + path + "\n"
        if command.startswith("find /media/fat/games/TIC-80/Carts"):
            return "1:2:30:4:5:regular file /media/fat/games/TIC-80/Carts/user.tic\n"
        if "load_core " in command:
            tokens = shlex.split(command)
            payload = tokens[tokens.index("printf") + 2]
            path = payload[len("load_core "):].strip()
            self.core = "MENU" if path.endswith("menu.rbf") else ("PICO-8" if path == self.original else "TIC-80")
            self.pid = str(int(self.pid) + 1)
            return ""
        if "devmem" in command:
            cart = next(data for path, data in self.files.items() if path.endswith("/quiet.tic"))
            return "\n".join(hex(value) for value in (0x314e5354, 6, len(cart), 6)) + "\n"
        if command.startswith("/lib/ld-linux-armhf.so.3 --list"):
            return "libc.so.6 => /lib/libc.so.6\n"
        if command.startswith("nohup sh "):
            self.launches += 1
            if self.fault == "launch-reply-lost":
                raise ConnectionError("SSH lost after the original job was launched")
            if self.fault == "user-core-change":
                self.core = "Gundam EX"
            return ""
        if command.startswith("if test -f ") and command.endswith("/status; fi"):
            return "0\n"
        if command.startswith("cat ") and command.endswith("/native.jsonl"):
            rows = [{"main_fd_verified": True, "evdev_grab_busy": True,
                     "only_main_and_probe_evdev_fds": True} for _ in range(4)]
            rows.append({"native_reader_passed": True, "total": 4})
            return "\n".join(json.dumps(row) for row in rows) + "\n"
        if command.startswith("cat ") and command.endswith("/native.stderr"):
            return ""
        if command.startswith(("test -f ", "test ! -e ", "chmod 755 ")):
            return ""
        raise AssertionError("Unexpected remote command: " + command)


class NativeDriverTests(unittest.TestCase):
    def run_driver(self, fault=None):
        board = Board(fault)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build = root / "probe"
            (build / "arm").mkdir(parents=True)
            stub = b"Local SSH-boundary test stub; never executed or sent over a real network"
            (build / "arm/tic80-hid-pan-probe").write_bytes(stub)
            source_hashes = {}
            for name in ("tools/hid_pan_probe.c", "src/hid_wheel.c", "src/hid_pan.c",
                         "include/tic80_mister/hid_wheel.h", "include/tic80_mister/hid_pan.h"):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / name).read_bytes())
                source_hashes[name] = driver.hashlib.sha256(target.read_bytes()).hexdigest()
            archive = root / "releases" / f"TIC80-Frontier-{driver.RELEASE}.zip"
            archive.parent.mkdir()
            manifest = "".join(digest + "  " + path.removeprefix("/media/fat/") + "\n"
                               for path, digest in board.hashes.items())
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("Scripts/TIC80-install/files.sha256", manifest)
            (build / "result.json").write_text(json.dumps({
                "passed": True, "source_sha256": source_hashes,
                "ARM_probe_sha256": driver.hashlib.sha256(stub).hexdigest()}))
            evidence = root / "evidence"
            argv = ["test", "--evidence", str(evidence), "--probe-build", str(build),
                    "--initial-core", "PICO-8"]
            error = None
            with patch.object(sys, "argv", argv), patch.object(driver.paramiko, "SSHClient", return_value=board), \
                 patch.object(driver, "connect"), patch.object(driver, "command", side_effect=board.run), \
                 patch.object(driver, "ROOT", root), \
                 patch.object(driver, "RELEASE_SHA", driver.hashlib.sha256(archive.read_bytes()).hexdigest()), \
                 patch.dict(driver.os.environ, {"TM_SSH_PASSWORD": "local-test-placeholder"}):
                try:
                    driver.main()
                except BaseException as caught:
                    error = caught
            result = json.loads((evidence / "result.json").read_text())
        return board, result, error

    def test_success_restores_original_core_without_replacing_installed_files(self):
        board, result, error = self.run_driver()
        self.assertIsNone(error)
        self.assertTrue(result["passed"])
        self.assertTrue(result["restored_verified"])
        self.assertEqual(board.core, "PICO-8")
        self.assertEqual(board.launches, 1)
        self.assertEqual(result["before"], result["after"])
        self.assertFalse(result["installed_payloads_replaced"])
        self.assertTrue(all(path.startswith("/tmp/tic80-hid-native-") for path in board.files))
        self.assertEqual(sum("load_core " in command for command in board.commands), 4)

    def test_lost_launch_reply_collects_original_job_without_relaunch(self):
        board, result, error = self.run_driver("launch-reply-lost")
        self.assertIsInstance(error, ConnectionError)
        self.assertFalse(result["passed"])
        self.assertEqual(board.launches, 1)
        self.assertEqual(result["original_probe_exit"], 0)
        self.assertTrue(result["restored_verified"])
        self.assertEqual(board.core, "PICO-8")
        self.assertIn("ConnectionError", result["error"])

    def test_user_core_change_is_not_overwritten_by_rollback(self):
        board, result, error = self.run_driver("user-core-change")
        self.assertIsNotNone(error)
        self.assertFalse(result["passed"])
        self.assertEqual(board.core, "Gundam EX")
        self.assertEqual(sum("load_core " in command for command in board.commands), 2)
        self.assertIn("do not overwrite user selection", result["restoration_error"])

    def test_wrong_main_refuses_every_mutation(self):
        board, result, error = self.run_driver("wrong-main")
        self.assertIsNotNone(error)
        self.assertFalse(result["passed"])
        self.assertEqual(result["dispatches"], [])
        self.assertEqual(board.core, "PICO-8")
        self.assertEqual(board.files, {})


if __name__ == "__main__":
    unittest.main()
