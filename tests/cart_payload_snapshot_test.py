"""Reject wrong deliveries without modifying the FPGA memory fixture."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY = sys.argv.pop(1)


class SnapshotTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.memory = self.root / "memory"
        self.output = self.root / "cart"
        self.payload = bytes(range(256)) * 7
        data = bytearray(0x450000)
        for offset, value in [(0, 0x33434954), (0x20, 17), (0x28, 17),
                              (0x70, 6), (0x74, len(self.payload)), (0x38, 6)]:
            struct.pack_into("<I", data, offset, value)
        data[0x50000:0x50000 + len(self.payload)] = self.payload
        self.memory.write_bytes(data)

    def run_snapshot(self, expected=0, ticket="6", size=None):
        before = hashlib.sha256(self.memory.read_bytes()).hexdigest()
        run = subprocess.run([BINARY, "--memory", str(self.memory), ticket,
                              str(len(self.payload) if size is None else size), str(self.output)],
                             capture_output=True, timeout=5)
        self.assertEqual(hashlib.sha256(self.memory.read_bytes()).hexdigest(), before)
        if expected == 0:
            self.assertEqual(run.returncode, 0, run.stderr.decode())
        else:
            self.assertNotEqual(run.returncode, 0)
            self.assertFalse(self.output.exists())
        return run

    def test_exact_delivery_and_no_memory_writes(self):
        result = self.run_snapshot()
        self.assertEqual(self.output.read_bytes(), self.payload)
        self.assertEqual(json.loads(result.stdout),
                         dict(ticket=6, bytes=len(self.payload), session=17, read_only=True))

    def test_wrong_ticket_size_and_invalid_arguments(self):
        for ticket, size in [("10", None), ("6", 1), ("-2", None),
                             ("4294967298", None), ("6", 0), ("6", 0x400001)]:
            with self.subTest(ticket=ticket, size=size):
                self.run_snapshot(1, ticket, size)

    def test_unacknowledged_or_reconfigured_delivery(self):
        for offset, value in [(0, 0), (0x38, 2), (0x20, 18), (0x28, 0), (0x4c204, 5)]:
            with self.subTest(offset=offset):
                original = self.memory.read_bytes()
                data = bytearray(original)
                struct.pack_into("<I", data, offset, value)
                self.memory.write_bytes(data)
                self.run_snapshot(1)
                self.memory.write_bytes(original)

    def test_existing_output_preserved(self):
        self.output.write_bytes(b"existing")
        run = subprocess.run([BINARY, "--memory", str(self.memory), "6",
                              str(len(self.payload)), str(self.output)], capture_output=True, timeout=5)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(self.output.read_bytes(), b"existing")


if __name__ == "__main__":
    unittest.main(verbosity=2)
