// Core-specific UIO 0x45: command, signed delta low word, high word. Commit
// exactly once at transaction end. Generic hps_io ignores this command.
module tic80_horizontal_wheel (
    input logic clk, reset, osd_open,
    input logic io_enable, io_strobe,
    input logic [15:0] io_din,
    output logic [31:0] total
);
    logic [2:0] words;
    logic selected, blocked;
    logic [31:0] delta;
    (* async_reg = "true" *) logic [1:0] osd_sync;
    always_ff @(posedge clk) begin
        if (reset) begin
            words <= 0; selected <= 0; blocked <= 0; delta <= 0;
            total <= 0; osd_sync <= 0;
        end else begin
            osd_sync <= {osd_sync[0], osd_open};
            if (!io_enable) begin
                if (selected && words == 3 && !blocked && !osd_sync[1])
                    total <= total + delta;
                words <= 0; selected <= 0; blocked <= 0;
            end else begin
                if (osd_sync[1]) blocked <= 1;
                if (io_strobe) begin
                    // Saturation prevents oversized transactions wrapping back
                    // to a seemingly complete three-word packet.
                    if (words != 7) words <= words + 1'b1;
                    case (words)
                        0: selected <= io_din == 16'h0045;
                        1: delta[15:0] <= io_din;
                        2: delta[31:16] <= io_din;
                        default: ;
                    endcase
                end
            end
        end
    end
endmodule
