// Exercise the real core through the pinned platform's RAM1 reset handoff.
module ddr_reset_handoff (
    input logic clk, reset, reset_sample_delay,
    input logic waitrequest, readdatavalid,
    input logic [63:0] readdata,
    input logic ioctl_download, ioctl_wr,
    input logic [26:0] ioctl_addr,
    input logic [7:0] ioctl_dout,
    output logic ioctl_wait,
    output logic read_request, write_request,
    output logic [28:0] address,
    output logic [7:0] burstcount, byteenable,
    output logic [63:0] writedata,
    output logic platform_owns_bus
);
    logic [28:0] core_address;
    logic [7:0] core_burstcount, core_byteenable;
    logic [63:0] core_writedata, core_readdata;
    logic core_read, core_write, core_waitrequest, core_readdatavalid;
    // Copied verbatim in structure from sysmem_lite's RAM1 synchronization;
    // the test driver checks these assignments against the pinned source.
    logic ram1_reset_0 = 1'b1;
    logic ram1_reset_1 = 1'b1;
    logic reset_late = 1'b1;
    always_ff @(posedge clk) begin
        reset_late <= reset;
        // Delay the first sample by one clock in the optional skew cases,
        // modeling relative resolution of two independent reset synchronizers.
        ram1_reset_0 <= reset_sample_delay ? reset_late : reset;
        ram1_reset_1 <= ram1_reset_0;
    end
    f2sdram_safe_terminator #(64, 8) terminator (
        .clk(clk), .rst_req_sync(ram1_reset_1),
        .waitrequest_slave(core_waitrequest), .burstcount_slave(core_burstcount),
        .address_slave(core_address), .readdata_slave(core_readdata),
        .readdatavalid_slave(core_readdatavalid), .read_slave(core_read),
        .writedata_slave(core_writedata), .byteenable_slave(core_byteenable),
        .write_slave(core_write), .waitrequest_master(waitrequest),
        .burstcount_master(burstcount), .address_master(address),
        .readdata_master(readdata), .readdatavalid_master(readdatavalid),
        .read_master(read_request), .writedata_master(writedata),
        .byteenable_master(byteenable), .write_master(write_request)
    );
    assign platform_owns_bus = terminator.terminating;
    tic80_video_top core (
        .clk_sys(clk), .clk_vid(clk), .clk_audio(clk), .ce_pix(1'b1), .reset(reset),
        .reset_sys_active(), .reset_vid_active(),
        .joystick_0(32'd0), .joystick_1(32'd0), .joystick_2(32'd0), .joystick_3(32'd0),
        .osd_status(32'd0), .osd_open(1'b0), .ps2_key(11'd0),
        .ps2_mouse(25'd0), .ps2_mouse_ext(16'd0),
        .horizontal_wheel(32'd0),
        .ioctl_download(ioctl_download), .ioctl_wr(ioctl_wr), .ioctl_addr(ioctl_addr),
        .ioctl_dout(ioctl_dout), .ioctl_index(16'd0), .ioctl_wait(ioctl_wait),
        .ddr_busy(core_waitrequest), .ddr_burstcnt(core_burstcount),
        .ddr_addr(core_address), .ddr_rd(core_read), .ddr_dout(core_readdata),
        .ddr_dout_ready(core_readdatavalid), .ddr_din(core_writedata),
        .ddr_be(core_byteenable), .ddr_we(core_write),
        .r(), .g(), .b(), .hs(), .vs(), .de(), .active(), .audio_l(), .audio_r()
    );
endmodule
