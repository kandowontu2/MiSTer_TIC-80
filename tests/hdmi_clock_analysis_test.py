"""Replay measured working and rejected HDMI clocks, plus lost-lock cases."""
import json
import sys
import unittest
from copy import deepcopy
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
from analyze_hdmi_clock import analyze


class ClockAnalysis(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.capture = json.loads((root / 'tests/data/hdmi-720p-cts.json').read_text())

    def test_working_output(self):
        result = analyze(self.capture['verified_before']['rows'])
        self.assertEqual(result['inferred_pixel_clock_min_hz'], 74250000)
        self.assertEqual(result['inferred_pixel_clock_max_hz'], 74250000)

    def test_rejected_output_with_same_vic_and_lock(self):
        rows = self.capture['rejected_candidate']['rows']
        self.assertTrue(all(row['detected_vic'] == 4 and row['transmitter_pll_locked'] for row in rows))
        with self.assertRaises(AssertionError): analyze(rows)

    def test_measured_one_cts_count_step(self):
        capture = json.loads((root / 'tests/data/hdmi-720p-one-cts-step.json').read_text())
        result = analyze(capture['rows'])
        self.assertEqual(result['inferred_pixel_clock_min_hz'], 74250000)
        self.assertEqual(result['inferred_pixel_clock_max_hz'], 74251000)
        self.assertAlmostEqual(result['maximum_error_ppm'], 13.468013468, places=8)

    def test_lock_drop_and_unavailable_measurement(self):
        rows = deepcopy(self.capture['verified_before']['rows'])
        rows[-1]['registers']['0x9e'] = '0x04'
        with self.assertRaises(AssertionError): analyze(rows)
        rows = deepcopy(self.capture['verified_before']['rows'])
        rows[-1]['registers']['0x0a'] = '0x80'
        with self.assertRaises(AssertionError): analyze(rows)


if __name__ == '__main__': unittest.main()
