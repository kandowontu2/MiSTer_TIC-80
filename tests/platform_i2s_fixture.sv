// Exercise the complete pinned platform audio module, including its real CE
// generator and mixer. Observational aliases do not change the DUT logic.
module platform_i2s_fixture (
    input clk, reset, sample_rate,
    input [15:0] left_in, right_in,
    output sclk, lrclk, sdata, ce, sample_ce, audio_enabled, active_reset,
    output [15:0] left_sample, right_sample
);
audio_out dut (
    .clk(clk), .reset(reset), .sample_rate(sample_rate),
    .flt_rate(32'd0), .cx(40'd0), .cx0(8'd0), .cx1(8'd0), .cx2(8'd0),
    .cy0(24'd0), .cy1(24'd0), .cy2(24'd0),
    .att(5'd0), .mix(2'd0), .is_signed(1'b1),
    .core_l(16'd0), .core_r(16'd0), .alsa_l(left_in), .alsa_r(right_in),
    .i2s_bclk(sclk), .i2s_lrclk(lrclk), .i2s_data(sdata),
    .spdif(), .dac_l(), .dac_r()
);
assign ce = dut.i2s_ce;
assign left_sample = dut.al;
assign right_sample = dut.ar;
assign sample_ce = dut.sample_ce;
assign audio_enabled = dut.a_en2;
assign active_reset = reset;
endmodule
