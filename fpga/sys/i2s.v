// MiSTer I2S serializer, with clock-phase-preserving reset handling.
// The interface and normal word ordering match the pinned platform module.
module i2s
#(
	parameter AUDIO_DW = 16
)
(
	input      reset,
	input      clk,
	input      ce,

	output sclk,
	output lrclk,
	output sdata,

	input [AUDIO_DW-1:0] left_chan,
	input [AUDIO_DW-1:0] right_chan
);

// Quartus 17's Verilog input mode ignores an initializer on an ANSI output
// declaration. Module-scope register initializers retain the required power-up
// values, including the high initial channel-select state.
reg sclk_state = 0;
reg lrclk_state = 1;
reg sdata_state = 0;
assign sclk = sclk_state;
assign lrclk = lrclk_state;
assign sdata = sdata_state;

reg [7:0] bit_cnt = 1;
reg msclk = 0;
reg reset_pending = 1;
reg [AUDIO_DW-1:0] left = 0;
reg [AUDIO_DW-1:0] right = 0;

always @(posedge clk) begin
	// Keep BCLK running through reset. Data/LRCLK change only while it is
	// high, one base cycle before its falling edge, including reset service.
	sclk_state <= msclk;
	if (reset) reset_pending <= 1;
	if (ce) begin
		msclk <= ~msclk;
		if (msclk) begin
			// Latch short requests until a safe data-update phase is available.
			if (reset || reset_pending) begin
				bit_cnt <= 1;
				lrclk_state <= 1;
				sdata_state <= 0;
				left <= 0;
				right <= 0;
				reset_pending <= 0;
			end
			else begin
				if (bit_cnt >= AUDIO_DW) begin
					bit_cnt <= 1;
					lrclk_state <= ~lrclk_state;
					if (lrclk_state) begin
						left <= left_chan;
						right <= right_chan;
					end
				end
				else bit_cnt <= bit_cnt + 1'd1;
				sdata_state <= lrclk_state ? right[AUDIO_DW - bit_cnt] : left[AUDIO_DW - bit_cnt];
			end
		end
	end
end

endmodule
