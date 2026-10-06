"""Reject incomplete or wrongly phased HDMI output timing evidence."""
import csv
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_hdmi_output_timing import analyze, MASTERS, PORTS


def fixture():
    rows = []
    for model in ('slow', 'fast'):
        for temperature in (-40, 100):
            for branch, master in MASTERS.items():
                for check in ('setup', 'hold'):
                    for endpoint in sorted(PORTS):
                        # Independently specified arrival/required times.
                        arrival, required = (10, 11) if check == 'setup' else (11, 10)
                        rows.append(dict(model=model, temperature=temperature, branch=branch,
                            check=check, endpoint=endpoint, launch_clock=master,
                            capture_clock='tm_hdmi_forwarded_' + branch, period=8,
                            inverted=1, waveform='4 8', slack=1,
                            relationship=4 if check == 'setup' else -4,
                            launch_time=0 if check == 'setup' else 8, latch_time=4,
                            arrival=arrival, required=required))
    return rows


def table(rows):
    out = io.StringIO()
    writer = csv.DictWriter(out, fieldnames=fixture()[0].keys(), delimiter='\t')
    writer.writeheader()
    writer.writerows(rows)
    return out.getvalue()


class Timing(unittest.TestCase):
    def test_complete_table_is_conditional_only(self):
        result = analyze(table(fixture()))
        self.assertEqual(result['paths'], 432)
        self.assertTrue(result['conditional_zero_skew_checks_passed'])
        self.assertFalse(result['external_qualified'])
        self.assertEqual(result['branches']['hdmi']['conditional_additional_data_minus_clock_delay_ns'],
                         dict(minimum=-1, maximum=1))

    def test_missing_bit_and_duplicate_endpoint_rejected(self):
        rows = fixture()
        with self.assertRaisesRegex(ValueError, 'Missing'):
            analyze(table(rows[:-1]))
        rows[-1] = rows[-2]
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            analyze(table(rows))

    def test_clock_and_edge_errors_rejected(self):
        for field, value, error in (
            ('launch_clock', MASTERS['direct'], 'Clock branch'),
            ('capture_clock', 'tm_hdmi_forwarded_direct', 'Clock branch'),
            ('inverted', 0, 'inversion'),
            ('waveform', '0 4', 'waveform'),
            ('waveform', 'nan 8', 'waveform'),
            ('relationship', 8, 'relationship'),
            ('relationship', -4, 'relationship'),
            ('launch_time', 1, 'relationship'),
            ('slack', float('nan'), 'Non-finite'),
            ('slack', 999, 'Slack disagrees'),
        ):
            rows = fixture(); rows[0][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, error):
                analyze(table(rows))

    def test_negative_slack_is_retained_as_a_failure(self):
        for check in ('setup', 'hold'):
            rows = fixture()
            row = next(row for row in rows if row['check'] == check)
            row.update(slack=-.25, arrival=11 if check == 'setup' else 10,
                       required=10.75 if check == 'setup' else 10.25)
            result = analyze(table(rows))
            self.assertFalse(result['conditional_zero_skew_checks_passed'])
            self.assertFalse(result['external_qualified'])
            self.assertEqual(result['branches']['hdmi']['checks'][check]['violated_paths'], 1)


if __name__ == '__main__':
    unittest.main()
