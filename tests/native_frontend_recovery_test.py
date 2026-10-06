"""Negative controls for the real-board save/recovery oracle; no hardware."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_frontend_recovery import validate_checkpoint, delivered, ordinary, cartridge
from native_frontend_lifecycle import code


class Oracle(unittest.TestCase):
    def test_error_ticket_is_required_for_short_transfer_and_ack_matches(self):
        self.assertTrue(delivered(7, 0, 7, 0, 0))
        self.assertFalse(delivered(6, 0, 6, 0, 0))
        self.assertFalse(delivered(7, 0, 6, 0, 0))
        self.assertTrue(delivered(10, 4, 10, 0, 4))
        self.assertFalse(delivered(11, 4, 11, 0, 4))
        self.assertFalse(delivered(10, 3, 10, 0, 4))
        with self.assertRaises(AssertionError):
            delivered(10, 4, 10, 1, 4)

    def test_unfinished_tick_cannot_satisfy_last_completed_checkpoint(self):
        values = [0] * 256
        values[0] = 2
        validate_checkpoint(values, last_complete=2)
        values[0] = 9999
        with self.assertRaises(AssertionError):
            validate_checkpoint(values, last_complete=2)

    def test_old_counter_or_extra_boot_cannot_satisfy_recovery(self):
        values = [0] * 256
        values[:2] = [240, 1]
        validate_checkpoint(values, minimum_ticks=240, boots=1)
        for ticks, boots in ((239, 1), (240, 2)):
            values[:2] = [ticks, boots]
            with self.assertRaises(AssertionError):
                validate_checkpoint(values, minimum_ticks=240, boots=1)

    def test_private_fixture_retains_exact_saveid_and_runaway_code(self):
        good = code(ordinary('private-only'))
        self.assertIn(b'-- saveid: private-only\n', good)
        self.assertIn(b'pmem(1,pmem(1)+1)', good)
        bad = code(cartridge('private-bad', 'function TIC() while true do end end\n'))
        self.assertIn(b'-- saveid: private-bad\n', bad)
        self.assertTrue(bad.endswith(b'function TIC() while true do end end\n'))


if __name__ == '__main__':
    unittest.main()
