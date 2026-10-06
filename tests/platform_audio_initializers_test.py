"""Keep simulator declaration conversion scoped and fail closed on drift."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from platform_audio_initializers import normalize

SOURCE = '''module audio_out;
// unrelated div and cnt must stay untouched
localparam AUDIO_RATE = 48000;
reg sample_ce;
always @(posedge clk) begin
 reg [8:0] div = 0;
 reg [1:0] add = 0;
 div <= div + add;
 sample_ce <= !div;
end
reg flt_ce;
always @(posedge clk) begin
 reg [31:0] cnt = 0;
 cnt = cnt + rate;
 flt_ce = cnt >= period;
end
reg [15:0] cl,cr;
reg a_en1 = 0, a_en2 = 0;
always @(posedge clk, posedge reset) begin
 reg [1:0] dly1 = 0;
 reg [14:0] dly2 = 0;
 if(reset) begin dly1 <= 0; dly2 <= 0; end
 else begin dly1 <= dly1 + 1; dly2 <= dly2 + 1; end
end
wire [15:0] acl, acr;
endmodule
'''


class Initializers(unittest.TestCase):
    def test_only_five_declarations_and_scoped_names_change(self):
        result, fields = normalize(SOURCE)
        self.assertEqual(len(fields), 5)
        self.assertIn('// unrelated div and cnt must stay untouched', result)
        self.assertIn('tm_static_sample_div <= tm_static_sample_div + tm_static_sample_add;', result)
        self.assertIn('tm_static_filter_cnt = tm_static_filter_cnt + rate;', result)
        self.assertIn('tm_static_enable_dly2 <= tm_static_enable_dly2 + 1;', result)
        self.assertLess(result.index('reg [8:0] tm_static_sample_div = 0;'), result.index('always @'))
        self.assertIn('reg a_en1 = 0, a_en2 = 0;', result)

    def test_source_drift_is_rejected(self):
        for changed in (
            SOURCE.replace('div = 0;', 'div = 1;'),
            SOURCE.replace('reg sample_ce;', 'reg sample_ce_v2;'),
            SOURCE.replace('reg [1:0] add = 0;', 'reg [1:0] add = 0;\n reg spare = 0;'),
            SOURCE.replace('endmodule', 'wire tm_static_sample_div;\nendmodule'),
            SOURCE.replace('localparam AUDIO_RATE = 48000;', 'localparam AUDIO_RATE = 44100;'),
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                normalize(changed)


if __name__ == '__main__': unittest.main()
