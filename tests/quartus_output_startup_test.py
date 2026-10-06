"""Reject missing or ambiguous compiler startup evidence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_quartus_output_startup import check

# Reduced from Quartus 17's actual initialized-internal-register output cone.
SOURCE = r"""module startup(clk, out);
output out;
assign gnd = 1'b0;
assign vcc = 1'b1;
assign out = \out~output_o ;
cyclonev_io_obuf \out~output (
 .i(!\state~q ),
 .oe(vcc),
 .o(\out~output_o ));
dffeas state(
 .clk(clk), .d(gnd), .clrn(vcc), .prn(vcc), .aload(gnd),
 .q(\state~q ));
defparam state.power_up = "low";
endmodule
"""


class StartupEvidence(unittest.TestCase):
    def test_physical_encoding_and_output_inversion_are_distinct(self):
        observation = check(SOURCE, {'out': 1})['out']
        self.assertTrue(observation['passed'])
        self.assertEqual(observation['trace'][-1]['physical_register_power_up'], 0)
        self.assertFalse(check(SOURCE, {'out': 0})['out']['passed'])

    def test_incomplete_ambiguous_or_disabled_cones_are_rejected(self):
        for changed in (
            SOURCE.replace('defparam state.power_up = "low";', ''),
            SOURCE.replace('defparam state.power_up = "low";', 'defparam state.power_up = "unknown";'),
            SOURCE.replace('.oe(vcc)', '.oe(gnd)'),
            SOURCE.replace('.clrn(vcc)', '.clrn(gnd)'),
            SOURCE.replace('.prn(vcc)', '.prn(gnd)'),
            SOURCE.replace('.aload(gnd)', '.aload(vcc)'),
            SOURCE.replace('.i(!\\state~q )', '.i(unmodeled_signal)'),
            SOURCE.replace('assign out = \\out~output_o ;', 'assign out = out;'),
            SOURCE.replace('endmodule', "assign out = 1'b1;\nendmodule"),
            SOURCE.replace('endmodule', 'defparam state.power_up = "high";\nendmodule'),
            SOURCE.replace('output out;', 'output different_out;'),
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                check(changed, {'out': 1})


if __name__ == '__main__':
    unittest.main()
