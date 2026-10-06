// Stereo PCM ring reader with a 256-frame asynchronous FIFO. The consumer
// copies two published sample frames per DDR word. AUDIO_READ advances after
// playback, using a Gray counter across clocks. CLK_AUDIO is 24.576 MHz.
module tic80_audio (
    input logic clk_sys, clk_audio, reset,
    input logic session_reset, session_active,
    input logic [31:0] write_counter,
    output logic [31:0] read_counter,
    output logic [31:0] sample_counter, underrun_counter,
    output logic request,
    output logic [28:0] address,
    input logic ready,
    input logic [63:0] data,
    output logic [15:0] audio_l, audio_r
);
    `include "memory_map.svh"
    wire flush = reset | session_reset;
    (* ramstyle = "M10K, no_rw_check" *) logic [63:0] fifo [0:127];
    logic [7:0] write_binary, write_gray, read_binary, read_gray;
    (* async_reg = "true" *) logic [7:0] read_sync1, read_sync2, write_sync1, write_sync2;
    (* async_reg = "true" *) logic [1:0] audio_reset;
    logic [63:0] read_data;
    logic [8:0] sample_div;
    logic second_sample;
    logic [31:0] fetched_counter;
    logic [30:0] played_binary, played_gray;
    (* async_reg = "true" *) logic [30:1] played_sync1, played_sync2;
    logic [30:0] played_decoded;
    logic primed;
    // Two complete TIC ticks absorb producer scheduling jitter. A finite
    // clip shorter than two ticks must still drain: start it after 50 ms.
    (* async_reg = "true" *) logic prefill_sync1, prefill_sync2;
    logic prefill_ready;
    logic [11:0] startup_slots;
    logic [31:0] samples_binary, samples_gray, underruns_binary, underruns_gray;
    (* async_reg = "true" *) logic [31:0] samples_sync1, samples_sync2, underruns_sync1, underruns_sync2;
    logic [31:0] samples_decoded, underruns_decoded;
    genvar stat_bit;
    generate for (stat_bit = 0; stat_bit < 32; stat_bit = stat_bit + 1) begin : decode_stats
        assign samples_decoded[stat_bit] = ^samples_sync2[31:stat_bit];
        assign underruns_decoded[stat_bit] = ^underruns_sync2[31:stat_bit];
    end endgenerate
    wire [31:0] next_samples_binary = samples_binary + 32'd1;
    wire [31:0] next_underruns_binary = underruns_binary + 32'd1;
    genvar i;
    generate for (i = 1; i < 31; i = i + 1) begin : decode_played
        assign played_decoded[i] = ^played_sync2[30:i];
    end endgenerate
    // read_binary and played_binary advance on exactly the same audio edge
    // and reset together. Their lowest Gray bit is therefore identical,
    // including read-pointer wrap. Reuse its existing two-stage crossing.
    assign played_decoded[0] = (^played_sync2) ^ read_sync2[0];
    wire [30:0] next_played_binary = played_binary + 31'd1;
    wire [7:0] next_write_binary = write_binary + 8'd1;
    wire [7:0] next_write_gray = (next_write_binary >> 1) ^ next_write_binary;
    wire [7:0] next_read_binary = read_binary + 8'd1;
    wire [7:0] next_read_gray = (next_read_binary >> 1) ^ next_read_binary;
    wire full = write_gray == {~read_sync2[7:6], read_sync2[5:0]};
    wire empty = read_gray == write_sync2;
    wire [31:0] occupancy = write_counter - fetched_counter;
    assign request = !flush && session_active && !full && occupancy >= 2 && occupancy <= TM_AUDIO_CAPACITY;
    assign address = 29'((TM_PHYSICAL_BASE + TM_AUDIO_RING_OFFSET) >> 3) + {18'd0, fetched_counter[11:1]};
    always_ff @(posedge clk_sys or posedge flush) begin
        if (flush) begin
            write_binary <= 0;
            write_gray <= 0;
            read_counter <= 0;
            fetched_counter <= 0;
            played_sync1 <= 0;
            played_sync2 <= 0;
            read_sync1 <= 0;
            read_sync2 <= 0;
            samples_sync1 <= 0; samples_sync2 <= 0;
            underruns_sync1 <= 0; underruns_sync2 <= 0;
            sample_counter <= 0; underrun_counter <= 0;
            prefill_ready <= 0;
        end else begin
            if (session_active && write_counter >= 1600 && write_counter <= TM_AUDIO_CAPACITY)
                prefill_ready <= 1;
            read_sync1 <= read_gray;
            read_sync2 <= read_sync1;
            played_sync1 <= played_gray[30:1];
            played_sync2 <= played_sync1;
            read_counter <= {played_decoded, 1'b0};
            samples_sync1 <= samples_gray; samples_sync2 <= samples_sync1;
            underruns_sync1 <= underruns_gray; underruns_sync2 <= underruns_sync1;
            sample_counter <= samples_decoded; underrun_counter <= underruns_decoded;
            if (ready && request) begin
                fifo[write_binary[6:0]] <= data;
                write_binary <= next_write_binary;
                write_gray <= next_write_gray;
                fetched_counter <= fetched_counter + 32'd2;
            end
        end
    end
    always_ff @(posedge clk_audio or posedge flush) begin
        if (flush) audio_reset <= 2'b11;
        else audio_reset <= {audio_reset[0], 1'b0};
    end
    always_ff @(posedge clk_audio) begin
        read_data <= fifo[read_binary[6:0]];
        if (audio_reset[1]) begin
            read_binary <= 0;
            read_gray <= 0;
            write_sync1 <= 0;
            write_sync2 <= 0;
            sample_div <= 0;
            second_sample <= 0;
            audio_l <= 0;
            audio_r <= 0;
            played_binary <= 0;
            played_gray <= 0;
            primed <= 0;
            prefill_sync1 <= 0; prefill_sync2 <= 0;
            startup_slots <= 0;
            samples_binary <= 0; samples_gray <= 0;
            underruns_binary <= 0; underruns_gray <= 0;
        end else begin
            write_sync1 <= write_gray;
            write_sync2 <= write_sync1;
            prefill_sync1 <= prefill_ready;
            prefill_sync2 <= prefill_sync1;
            sample_div <= sample_div + 9'd1;
            if (sample_div == 511) begin
                if (!primed && !empty && startup_slots < 2399)
                    startup_slots <= startup_slots + 12'd1;
                // Ignore startup buffering before the first output PCM pair.
                // Once primed, count every 48 kHz output slot, including the
                // slots filled with silence. Gray counters cross to clk_sys.
                if (primed || (!empty && (prefill_sync2 || startup_slots == 2399))) begin
                    primed <= 1;
                    samples_binary <= next_samples_binary;
                    samples_gray <= (next_samples_binary >> 1) ^ next_samples_binary;
                    if (empty) begin
                        underruns_binary <= next_underruns_binary;
                        underruns_gray <= (next_underruns_binary >> 1) ^ next_underruns_binary;
                    end
                end
                if (!primed && !(prefill_sync2 || startup_slots == 2399)) begin
                    audio_l <= 0;
                    audio_r <= 0;
                end else if (empty) begin
                    audio_l <= 0;
                    audio_r <= 0;
                    second_sample <= 0;
                end else begin
                    {audio_r, audio_l} <= second_sample ? read_data[63:32] : read_data[31:0];
                    second_sample <= !second_sample;
                    if (second_sample) begin
                        read_binary <= next_read_binary;
                        read_gray <= next_read_gray;
                        played_binary <= next_played_binary;
                        played_gray <= (next_played_binary >> 1) ^ next_played_binary;
                    end
                end
            end
        end
    end
endmodule
