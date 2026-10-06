// 341 * 262 pixels at 60 Hz from MiSTer's 24.576 MHz audio clock.
// Sharing the playback clock prevents video/audio oscillator drift. The exact
// reduced ratio is 44671 / 204800; pixel intervals are four or five clocks,
// and each complete raster takes exactly 409600 clocks (800 audio samples).
module tic80_pixel_enable (
    input logic clk, reset,
    output wire ce_pix
);
    logic [17:0] phase;
    assign ce_pix = !reset && phase >= 18'd160129;
    always_ff @(posedge clk) begin
        if (reset) phase <= 0;
        else if (ce_pix) phase <= phase - 18'd160129;
        else phase <= phase + 18'd44671;
    end
endmodule
