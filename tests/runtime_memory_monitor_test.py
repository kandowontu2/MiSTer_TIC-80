"""Run the real Linux sampler against live processes and atomic save files."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib

parser = argparse.ArgumentParser()
parser.add_argument('monitor', type=Path)
args, remaining = parser.parse_known_args()
MONITOR = args.monitor.resolve()


class MemorySampler(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.core = self.folder / 'CORENAME'
        self.core.write_text('TIC-80\n')
        self.save = self.folder / 'test.pmem'
        self.write_save(100)
        self.worker = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(10)'],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(self.stop_worker)

    def stop_worker(self):
        if self.worker.poll() is None:
            self.worker.terminate()
        self.worker.wait(timeout=5)

    def write_save(self, ticks):
        values = [ticks, 200, 1, 15] + [0] * 252
        payload = struct.pack('<256I', *values)
        temporary = self.folder / 'test.new'
        temporary.write_bytes(b'TMPM' + struct.pack('<II', 1, zlib.crc32(payload)) + payload)
        temporary.replace(self.save)

    def command(self):
        return [str(MONITOR), '--parent', str(os.getpid()), '--worker', str(self.worker.pid),
            '--seconds', '1', '--interval-ms', '100', '--save', str(self.save), '--core-name', str(self.core)]

    def run_monitor(self):
        return subprocess.run(self.command(), text=True, capture_output=True, timeout=5)

    def test_live_identity_crc_and_monotonic_cadence(self):
        result = self.run_monitor()
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertGreaterEqual(len(rows), 10)
        self.assertTrue(all(row['parent'] == os.getpid() and row['worker'] == self.worker.pid for row in rows))
        self.assertEqual(len({row['parent_starttime'] for row in rows}), 1)
        self.assertEqual(len({row['worker_starttime'] for row in rows}), 1)
        self.assertTrue(all(row['save_CRC_valid'] and row['save_ticks'] == 100 for row in rows))
        self.assertTrue(all(row['parent_fds'] >= 3 and row['worker_fds'] >= 3 for row in rows))
        gaps = [(right['captured_monotonic_ns']-left['captured_monotonic_ns'])/1e9
            for left, right in zip(rows, rows[1:])]
        self.assertTrue(all(0 < gap < .5 for gap in gaps), gaps)

    def test_corrupt_save_is_rejected(self):
        data = bytearray(self.save.read_bytes())
        data[12] ^= 1
        self.save.write_bytes(data)
        self.assertEqual(self.run_monitor().returncode, 1)

    def test_core_departure_is_rejected(self):
        self.core.write_text('MENU\n')
        self.assertEqual(self.run_monitor().returncode, 1)

    def test_worker_departure_is_rejected_after_sampling(self):
        timer = threading.Timer(.35, self.stop_worker)
        timer.start()
        try:
            result = self.run_monitor()
        finally:
            timer.join()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertTrue(result.stdout.strip())

    def test_save_regression_is_rejected(self):
        timer = threading.Timer(.35, lambda: self.write_save(99))
        timer.start()
        try:
            result = self.run_monitor()
        finally:
            timer.join()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('completed save regressed', result.stderr)
        self.assertTrue(result.stdout.strip())

    def test_detached_sampling_survives_closed_observer_output(self):
        log = self.folder / 'detached.jsonl'
        with log.open('w') as destination:
            process = subprocess.Popen(self.command(), stdout=destination, stderr=subprocess.PIPE,
                stdin=subprocess.DEVNULL, text=True, start_new_session=True)
        # Closing the observer's descriptor does not close the child's file.
        _, stderr = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 0, stderr)
        self.assertGreaterEqual(len(log.read_text().splitlines()), 10)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
