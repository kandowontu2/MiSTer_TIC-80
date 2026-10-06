module tic80_video_top (
    input logic clk_sys, clk_vid, clk_audio, ce_pix, reset,
    output wire reset_sys_active, reset_vid_active,
    input logic [31:0] joystick_0, joystick_1, joystick_2, joystick_3,
    input logic [31:0] osd_status,
    input logic osd_open,
    input logic [10:0] ps2_key,
    input logic [24:0] ps2_mouse,
    input logic [15:0] ps2_mouse_ext,
    input logic [31:0] horizontal_wheel,
    input logic ioctl_download, ioctl_wr,
    input logic [26:0] ioctl_addr,
    input logic [7:0] ioctl_dout,
    input logic [15:0] ioctl_index,
    output logic ioctl_wait,
    input logic ddr_busy,
    output logic [7:0] ddr_burstcnt,
    output logic [28:0] ddr_addr,
    output logic ddr_rd,
    input logic [63:0] ddr_dout,
    input logic ddr_dout_ready,
    output logic [63:0] ddr_din,
    output logic [7:0] ddr_be,
    output logic ddr_we,
    output logic [7:0] r, g, b,
    output logic hs, vs, de, active,
    output logic [15:0] audio_l, audio_r
);
    // sysmem_lite synchronizes RESET for two RAM clocks, then its safe
    // terminator captures a stalled request on the next edge. Keep the
    // memory master and its cart/audio data alive through that handoff.
    // An extra edge allows relative sampling skew between the synchronizers.
    // Initial reset prevents DDR requests before this clock/PLL is ready.
    (* async_reg = "true" *) logic [3:0] reset_sys = 4'b1111;
    (* async_reg = "true" *) logic [1:0] reset_vid;
    // All custom consumers in each domain share its synchronized reset.
    // The DDR-side assertion delay preserves the terminator handoff.
    assign reset_sys_active = reset_sys[3];
    assign reset_vid_active = reset_vid[1];
    always_ff @(posedge clk_sys)
        reset_sys <= {reset_sys[2:0], reset};
    always_ff @(posedge clk_vid or posedge reset)
        if (reset) reset_vid <= 2'b11;
        else reset_vid <= {reset_vid[0], 1'b0};
    logic vblank_toggle;
    (* async_reg = "true" *) logic [2:0] vblank_sync;
    always_ff @(posedge clk_sys) begin
        if (reset_sys[3]) vblank_sync <= 0;
        else vblank_sync <= {vblank_sync[1:0], vblank_toggle};
    end
    wire vblank_start = vblank_sync[2] ^ vblank_sync[1];
    logic frame_write, frame_write_bank, frame_bank, frame_toggle, source_valid;
    logic [14:0] frame_write_address;
    logic [63:0] frame_write_data;
    logic consumed_toggle;
    (* async_reg = "true" *) logic [2:0] consumed_sync;
    always_ff @(posedge clk_sys) begin
        if (reset_sys[3]) consumed_sync <= 0;
        else consumed_sync <= {consumed_sync[1:0], consumed_toggle};
    end
    logic audio_request, audio_ready, audio_session_reset, audio_session_active;
    logic [28:0] audio_address;
    logic [31:0] audio_write_counter, audio_read_counter;
    logic [31:0] audio_samples, audio_underruns;
    logic [31:0] cart_ack;
    logic cart_ack_valid, cart_write, cart_ready;
    logic [28:0] cart_address;
    logic [63:0] cart_data, cart_meta;
    logic [7:0] cart_be;
    logic [511:0] keys;
    logic [63:0] mouse;
    tic80_input input_device (
        .clk(clk_sys), .reset(reset_sys[3]), .osd_open(osd_open),
        .ps2_key(ps2_key), .ps2_mouse(ps2_mouse), .ps2_mouse_ext(ps2_mouse_ext),
        .keys(keys), .mouse(mouse)
    );
    tic80_cart_loader cart_loader (
        .clk(clk_sys), .reset(reset_sys[3]), .ioctl_download(ioctl_download),
        .ioctl_wr(ioctl_wr), .ioctl_addr(ioctl_addr), .ioctl_dout(ioctl_dout),
        .ioctl_index(ioctl_index), .ioctl_wait(ioctl_wait), .cart_ack(cart_ack),
        .cart_ack_valid(cart_ack_valid), .cart_meta(cart_meta),
        .write_request(cart_write), .write_address(cart_address),
        .write_data(cart_data), .write_be(cart_be), .write_ready(cart_ready)
    );
    tic80_audio audio (
        .clk_sys(clk_sys), .clk_audio(clk_audio), .reset(reset_sys[3]),
        .session_reset(audio_session_reset), .session_active(audio_session_active),
        .write_counter(audio_write_counter), .read_counter(audio_read_counter),
        .sample_counter(audio_samples), .underrun_counter(audio_underruns),
        .request(audio_request), .address(audio_address), .ready(audio_ready),
        .data(ddr_dout), .audio_l(audio_l), .audio_r(audio_r)
    );
    tic80_ddr_video reader (
        .clk(clk_sys), .reset(reset_sys[3]), .vblank_start(vblank_start),
        .joystick_0(joystick_0), .joystick_1(joystick_1), .joystick_2(joystick_2), .joystick_3(joystick_3),
        .osd_status(osd_status), .cart_meta(cart_meta), .cart_ack(cart_ack), .cart_ack_valid(cart_ack_valid),
        .input_keys(keys), .input_mouse(mouse), .input_horizontal_wheel(horizontal_wheel),
        .cart_write(cart_write), .cart_address(cart_address), .cart_data(cart_data), .cart_be(cart_be), .cart_ready(cart_ready),
        .ddr_busy(ddr_busy), .ddr_burstcnt(ddr_burstcnt), .ddr_addr(ddr_addr),
        .ddr_rd(ddr_rd), .ddr_dout(ddr_dout), .ddr_dout_ready(ddr_dout_ready),
        .ddr_din(ddr_din), .ddr_be(ddr_be), .ddr_we(ddr_we),
        .consumed_toggle(consumed_sync[2]), .audio_request(audio_request),
        .audio_address(audio_address), .audio_ready(audio_ready),
        .audio_write_counter(audio_write_counter), .audio_read_counter(audio_read_counter),
        .audio_samples(audio_samples), .audio_underruns(audio_underruns),
        .audio_session_reset(audio_session_reset), .audio_session_active(audio_session_active),
        .frame_write(frame_write), .frame_write_bank(frame_write_bank),
        .frame_write_address(frame_write_address), .frame_write_data(frame_write_data),
        .frame_bank(frame_bank), .frame_toggle(frame_toggle), .source_valid(source_valid)
    );
    tic80_scanout scanout (
        .clk_sys(clk_sys), .clk_vid(clk_vid), .reset(reset_vid[1]), .ce_pix(ce_pix),
        .frame_write(frame_write), .frame_write_bank(frame_write_bank),
        .frame_write_address(frame_write_address), .frame_write_data(frame_write_data),
        .frame_bank(frame_bank), .frame_toggle(frame_toggle), .source_valid(source_valid),
        .r(r), .g(g), .b(b), .hs(hs), .vs(vs), .de(de), .vblank_toggle(vblank_toggle),
        .display_bank(), .display_valid(active), .consumed_toggle(consumed_toggle)
    );
endmodule
