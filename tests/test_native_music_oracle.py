"""Acceptance controls for the private native music/clock soak."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_music_soak import validate_samples


def samples(seconds=600):
    rows = []
    for n in range(int(seconds * 1000 / 197) + 1):
        ns = n * 197000000
        slots = ns * 48000 // 1000000000
        frame = ns * 60 // 1000000000
        rows.append(dict(elapsed_ns=ns, session=1, slots=slots, underruns=0,
                         played=slots, written=slots + 3200, publication=(frame + 1) << 2,
                         presented=(frame + 1) << 2, heartbeat=frame + 1))
    return rows


class MusicOracle(unittest.TestCase):
    def check(self, rows, seconds=600):
        return validate_samples('\n'.join(map(json.dumps, rows)), seconds)

    def test_full_soak(self):
        result = self.check(samples())
        self.assertEqual(result['audio_clock_hz'], 48000)
        self.assertEqual(result['audio_queue']['fitted_queue_change_ms'], 0)

    def test_faults_are_not_accepted(self):
        for field, value in [('session', 2), ('underruns', 1), ('played', 1),
                             ('written', 0xffffffff), ('heartbeat', 999999)]:
            with self.subTest(field=field):
                rows = samples(); rows[100][field] = value
                with self.assertRaises(AssertionError): self.check(rows)
        # Maintain coherent played/slot equality while changing the clock.
        rows = samples()
        for row in rows:
            row['slots'] = row['played'] = row['elapsed_ns'] * 47000 // 1000000000
            row['written'] = row['slots'] + 3200
        with self.assertRaises(AssertionError): self.check(rows)
        rows = samples()
        for row in rows: row['written'] += row['elapsed_ns'] // 1000000000
        with self.assertRaises(AssertionError): self.check(rows)

    def test_short_or_truncated_run_fails(self):
        with self.assertRaises(AssertionError): self.check(samples()[:100])
        with self.assertRaises(AssertionError): self.check(samples()[:-20])


if __name__ == '__main__': unittest.main()
