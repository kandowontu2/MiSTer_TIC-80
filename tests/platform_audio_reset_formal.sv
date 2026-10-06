module platform_audio_reset_formal(input clk, reset_async);
    wire reset_audio;
    tic80_audio_reset dut(.clk(clk), .reset_async(reset_async), .reset_audio(reset_audio));
    reg [2:0] past_valid = 0;
    always @(posedge clk) begin
        past_valid <= {past_valid[1:0], 1'b1};
        if (!past_valid[0]) assert(reset_audio);
        if (reset_async) assert(reset_audio);
        if (past_valid[2]) begin
            if ($past(reset_async) || $past(reset_async, 2) || $past(reset_async, 3))
                assert(reset_audio);
            else if (!reset_async) assert(!reset_audio);
        end
    end
endmodule
