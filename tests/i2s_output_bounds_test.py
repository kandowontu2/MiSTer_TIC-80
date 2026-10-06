"""Check edge polarity, signed intervals and rejection of incomplete evidence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_i2s_output_bounds import AUDIO_CLOCK, CORNERS, analyze

REPORT = f''' ; Clock to Output Times ;
; HDMI_SCLK ; FPGA_CLK3_50 ; 20 ; 100 ; Rise ; {AUDIO_CLOCK} ;
; HDMI_I2S ; FPGA_CLK3_50 ; 29 ; 30 ; Rise ; {AUDIO_CLOCK} ;
; HDMI_LRCLK ; FPGA_CLK3_50 ; 39 ; 40 ; Rise ; {AUDIO_CLOCK} ;
; Minimum Clock to Output Times ;
; HDMI_SCLK ; FPGA_CLK3_50 ; 15 ; 10 ; Rise ; {AUDIO_CLOCK} ;
; HDMI_I2S ; FPGA_CLK3_50 ; 11 ; 10 ; Rise ; {AUDIO_CLOCK} ;
; HDMI_LRCLK ; FPGA_CLK3_50 ; 6 ; 5 ; Rise ; {AUDIO_CLOCK} ;
'''
TABLE = 'model\ttemperature\tclock\tperiod_ns\n' + ''.join(
    f'{model}\t{temperature}\t{AUDIO_CLOCK}\t40\n' for model, temperature in sorted(CORNERS))
PROOF = dict(passed=True, unbounded_proof=True, platform_enable_included=True,
    minimum_change_to_bclk_rise_base_cycles=3, minimum_bclk_rise_to_change_base_cycles=1)


class ConditionalI2S(unittest.TestCase):
    def test_both_data_polarities_but_only_rising_capture_edge(self):
        result = analyze(dict.fromkeys(CORNERS, REPORT), TABLE, PROOF)
        self.assertEqual(result['minimum_setup_slack_ns'], 93)
        self.assertEqual(result['minimum_hold_slack_ns'], 23)
        self.assertEqual(len(result['checks']), 8)
        self.assertTrue(result['conditional_pin_bound_analysis_passed'])
        self.assertFalse(result['external_qualified'])

    def test_negative_hold_is_reported_as_failure(self):
        result = analyze(dict.fromkeys(CORNERS, REPORT.replace('; 20 ; 100 ;', '; 60 ; 100 ;')), TABLE, PROOF)
        self.assertEqual(result['minimum_hold_slack_ns'], -17)
        self.assertFalse(result['conditional_pin_bound_analysis_passed'])
        tiny = analyze(dict.fromkeys(CORNERS, REPORT.replace('; 20 ; 100 ;', '; 43.000000001 ; 100 ;')), TABLE, PROOF)
        self.assertLess(tiny['minimum_hold_slack_ns'], 0)
        self.assertFalse(tiny['conditional_pin_bound_analysis_passed'])

    def test_missing_duplicate_or_invalid_pin_bounds_are_rejected(self):
        for changed in (REPORT.replace('HDMI_LRCLK', 'unused_pin'),
                        REPORT + REPORT, REPORT.replace('; Rise ;', '; Fall ;'),
                        REPORT.replace(AUDIO_CLOCK, 'wrong_clock'),
                        REPORT.replace('; 11 ; 10 ;', '; nan ; 10 ;'),
                        REPORT.replace('; 11 ; 10 ;', '; 40 ; 40 ;')):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                analyze(dict.fromkeys(CORNERS, changed), TABLE, PROOF)

    def test_clock_and_digital_proof_coverage_is_required(self):
        for table, proof in ((TABLE + TABLE.splitlines()[1] + '\n', PROOF),
                             (TABLE.replace('\t40\n', '\tinf\n'), PROOF),
                             (TABLE, dict(PROOF, unbounded_proof=False)),
                             (TABLE, dict(PROOF, platform_enable_included=False)),
                             (TABLE, dict(PROOF, minimum_change_to_bclk_rise_base_cycles=0))):
            with self.subTest(table=table, proof=proof), self.assertRaises(ValueError):
                analyze(dict.fromkeys(CORNERS, REPORT), table, proof)
        with self.assertRaises(ValueError):
            analyze({}, TABLE, PROOF)


if __name__ == '__main__':
    unittest.main()
