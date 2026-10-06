// Full-frame dual-clock BRAM scanout. DDR writes only the bank which is not
// being displayed; bank changes become visible during vertical blanking.
// ce_pix averages 5.36052 MHz from the shared 24.576 MHz playback/video clock.
module tic80_scanout (
    input logic clk_sys, clk_vid, reset, ce_pix,
    input logic frame_write, frame_write_bank,
    input logic [14:0] frame_write_address,
    input logic [63:0] frame_write_data,
    input logic frame_bank, frame_toggle, source_valid,
    output logic [7:0] r, g, b,
    output logic hs, vs, de,
    output logic vblank_toggle,
    output logic consumed_toggle,
    output logic display_bank, display_valid
);
    localparam integer WORDS = 256 * 144 / 2;
    (* ramstyle = "M10K, no_rw_check" *) logic [63:0] memory0 [0:WORDS-1];
    (* ramstyle = "M10K, no_rw_check" *) logic [63:0] memory1 [0:WORDS-1];
    logic [63:0] read0, read1;
    logic [14:0] read_address;
    logic [9:0] x;
    logic [8:0] y;
    logic [7:0] source_y;
    wire content = x < 256 && y >= 40 && y < 184;
    always_comb begin
        source_y = 8'(y - 9'd40);
        read_address = content ? {source_y, x[7:1]} : 15'd0;
    end
    always_ff @(posedge clk_sys) begin
        if (frame_write && frame_write_address < 15'(WORDS)) begin
            if (frame_write_bank) memory1[frame_write_address] <= frame_write_data;
            else memory0[frame_write_address] <= frame_write_data;
        end
    end
    always_ff @(posedge clk_vid) begin
        read0 <= memory0[read_address];
        read1 <= memory1[read_address];
    end
    (* async_reg = "true" *) logic [2:0] toggle_sync;
    (* async_reg = "true" *) logic [1:0] bank_sync, valid_sync;
    logic seen_toggle, pending, pending_bank;
    wire [63:0] word_data = display_bank ? read1 : read0;
    wire [31:0] pixel = word_data[{x[0], 5'b00000} +: 32];
    always_ff @(posedge clk_vid) begin
        if (reset) begin
            toggle_sync <= 0;
            bank_sync <= 0;
            valid_sync <= 0;
            seen_toggle <= 0;
            pending <= 0;
            pending_bank <= 0;
            display_bank <= 0;
            display_valid <= 0;
            x <= 0;
            y <= 0;
            vblank_toggle <= 0;
            consumed_toggle <= 0;
            r <= 0; g <= 0; b <= 0;
            hs <= 1; vs <= 1; de <= 0;
        end else begin
            toggle_sync <= {toggle_sync[1:0], frame_toggle};
            bank_sync <= {bank_sync[0], frame_bank};
            valid_sync <= {valid_sync[0], source_valid};
            if (!valid_sync[1]) begin
                display_valid <= 0;
                pending <= 0;
                seen_toggle <= toggle_sync[2];
                consumed_toggle <= toggle_sync[2];
            end else begin
                if (toggle_sync[2] != seen_toggle) begin
                    seen_toggle <= toggle_sync[2];
                    pending <= 1;
                    pending_bank <= bank_sync[1];
                end
                if (pending && y >= 224) begin
                    display_bank <= pending_bank;
                    display_valid <= 1;
                    consumed_toggle <= seen_toggle;
                    pending <= 0;
                end
            end
            if (ce_pix) begin
                hs <= !(x >= 277 && x < 302);
                vs <= !(y >= 237 && y < 240);
                // HDMI captures the actual TIC-80 image, so its scaler can
                // fill a 16:9 display without scaling the CRT's black margins.
                de <= content;
                if (content && display_valid && valid_sync[1]) begin
                    r <= pixel[7:0];
                    g <= pixel[15:8];
                    b <= pixel[23:16];
                end else begin
                    r <= 0; g <= 0; b <= 0;
                end
                if (x == 340) begin
                    x <= 0;
                    if (y == 261) y <= 0;
                    else y <= y + 1'b1;
                    if (y == 223) vblank_toggle <= !vblank_toggle;
                end else x <= x + 1'b1;
            end
        end
    end
endmodule
