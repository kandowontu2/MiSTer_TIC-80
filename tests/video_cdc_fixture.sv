// Observation-only wrapper around the production integration. Hierarchical
// probes expose ownership and raster state without replacing any handshake.
module video_cdc_fixture (
    input logic clk_sys, clk_vid, reset, ce_pix,
    input logic ddr_busy, ddr_dout_ready,
    input logic [63:0] ddr_dout,
    output wire [7:0] ddr_burstcnt, ddr_be,
    output wire [28:0] ddr_addr,
    output wire ddr_rd, ddr_we,
    output wire [63:0] ddr_din,
    output wire [7:0] r, g, b,
    output wire active, reset_sys_active, reset_vid_active,
    output wire frame_write, write_bank, display_bank, visible,
    output wire [14:0] write_address,
    output wire [63:0] write_data,
    output wire frame_toggle, consumed_toggle, session_reset,
    output wire [9:0] x,
    output wire [8:0] y
);
    tic80_video_top core (
        .clk_sys(clk_sys), .clk_vid(clk_vid), .clk_audio(clk_vid),
        .reset(reset), .ce_pix(ce_pix),
        .joystick_0(32'd0), .joystick_1(32'd0), .joystick_2(32'd0), .joystick_3(32'd0),
        .osd_status(32'd0), .osd_open(1'b0), .ps2_key(11'd0),
        .ps2_mouse(25'd0), .ps2_mouse_ext(16'd0), .horizontal_wheel(32'd0),
        .ioctl_download(1'b0), .ioctl_wr(1'b0), .ioctl_addr(27'd0),
        .ioctl_dout(8'd0), .ioctl_index(16'd0), .ioctl_wait(),
        .ddr_busy(ddr_busy), .ddr_dout_ready(ddr_dout_ready), .ddr_dout(ddr_dout),
        .ddr_burstcnt(ddr_burstcnt), .ddr_addr(ddr_addr), .ddr_rd(ddr_rd),
        .ddr_din(ddr_din), .ddr_be(ddr_be), .ddr_we(ddr_we),
        .reset_sys_active(reset_sys_active), .reset_vid_active(reset_vid_active),
        .r(r), .g(g), .b(b), .hs(), .vs(), .de(), .active(active),
        .audio_l(), .audio_r()
    );
    assign frame_write = core.frame_write;
    assign write_bank = core.frame_write_bank;
    assign write_address = core.frame_write_address;
    assign write_data = core.frame_write_data;
    assign frame_toggle = core.frame_toggle;
    assign consumed_toggle = core.consumed_sync[2];
    assign session_reset = core.audio_session_reset;
    assign display_bank = core.scanout.display_bank;
    assign visible = core.scanout.display_valid && core.scanout.valid_sync[1];
    assign x = core.scanout.x;
    assign y = core.scanout.y;
endmodule
