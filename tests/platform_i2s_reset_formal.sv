module platform_i2s_reset_formal (
    input clk, reset, ce,
    input [15:0] left_chan, right_chan
);
    wire sclk, lrclk, sdata;
    i2s dut (.*);
    reg [2:0] past_valid = 0;
    always @(posedge clk) begin
        past_valid <= {past_valid[1:0], 1'b1};
        if (past_valid[0]) begin
            // The platform clock-enable generator never emits adjacent pulses.
            // No reset, audio-word or longer enable-spacing restrictions apply.
            assume(!ce || !$past(ce));
            if (sdata != $past(sdata) || lrclk != $past(lrclk)) begin
                assert(sclk && $past(sclk));
                assert($past(ce));
            end
            if (sclk && !$past(sclk)) begin
                assert(sdata == $past(sdata));
                assert(lrclk == $past(lrclk));
            end
        end
        // A change is followed by two base-clock samples with BCLK low.
        // Together with the no-coincident-rise assertion above, the next
        // capture edge is at least three base cycles after the change.
        if (past_valid[1] && ($past(sdata) != $past(sdata, 2) || $past(lrclk) != $past(lrclk, 2)))
            assert(!sclk);
        if (past_valid[2] && ($past(sdata, 2) != $past(sdata, 3) || $past(lrclk, 2) != $past(lrclk, 3)))
            assert(!sclk);
    end
endmodule
