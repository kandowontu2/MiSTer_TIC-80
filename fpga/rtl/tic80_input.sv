// Capture MiSTer's decoded PS/2 events in clk_sys. Keep physical keys separate
// so releasing one of two Shift/Ctrl/Alt keys cannot release the other side.
module tic80_input (
    input logic clk, reset, osd_open,
    input logic [10:0] ps2_key,
    input logic [24:0] ps2_mouse,
    input logic [15:0] ps2_mouse_ext,
    output logic [511:0] keys,
    // [7:0] x, [15:8] y, [18:16] buttons; [30:19] OSD edge epoch;
    // [31] synchronized OSD-open; [63:32] vertical wheel total.
    output logic [63:0] mouse
);
    logic key_toggle, mouse_toggle;
    (* async_reg = "true" *) logic [1:0] osd_sync;
    logic [7:0] mouse_x, mouse_y;
    logic [2:0] mouse_buttons;
    logic [31:0] wheel;
    logic osd_previous;
    logic [11:0] osd_epoch;
    logic signed [10:0] x_next, y_next;
    logic signed [9:0] dx, dy;
    always_comb begin
        dx = ps2_mouse[6] ? (ps2_mouse[4] ? -10'sd255 : 10'sd255) :
             $signed({ps2_mouse[4], ps2_mouse[4], ps2_mouse[15:8]});
        dy = ps2_mouse[7] ? (ps2_mouse[5] ? -10'sd255 : 10'sd255) :
             $signed({ps2_mouse[5], ps2_mouse[5], ps2_mouse[23:16]});
        x_next = $signed({3'd0, mouse_x}) + {dx[9], dx};
        y_next = $signed({3'd0, mouse_y}) - {dy[9], dy};
    end
    assign mouse = {wheel, osd_sync[1], osd_epoch, mouse_buttons, mouse_y, mouse_x};
    always_ff @(posedge clk) begin
        if (reset) begin
            osd_sync <= 0;
            osd_previous <= 0;
            osd_epoch <= 0;
            key_toggle <= ps2_key[10];
            mouse_toggle <= ps2_mouse[24];
            keys <= 0;
            mouse_x <= 120;
            mouse_y <= 68;
            mouse_buttons <= 0;
            wheel <= 0;
        end else begin
            osd_sync <= {osd_sync[0], osd_open};
            osd_previous <= osd_sync[1];
            if (osd_previous != osd_sync[1]) osd_epoch <= osd_epoch + 12'd1;
            key_toggle <= ps2_key[10];
            mouse_toggle <= ps2_mouse[24];
            if (osd_sync[1]) begin
                keys <= 0;
                mouse_buttons <= 0;
            end else begin
                if (key_toggle != ps2_key[10]) keys[ps2_key[8:0]] <= ps2_key[9];
                if (mouse_toggle != ps2_mouse[24]) begin
                    mouse_x <= x_next < 0 ? 8'd0 : x_next > 239 ? 8'd239 : x_next[7:0];
                    mouse_y <= y_next < 0 ? 8'd0 : y_next > 135 ? 8'd135 : y_next[7:0];
                    mouse_buttons <= ps2_mouse[2:0];
                    wheel <= wheel + {{24{ps2_mouse_ext[7]}}, ps2_mouse_ext[7:0]};
                end
            end
        end
    end
endmodule
