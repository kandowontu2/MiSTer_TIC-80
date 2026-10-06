// TIC-80 integration for the MiSTer emu interface. The interface/header and
// platform framework are derived from MiSTer Menu / MiSTer PICO-8; see LICENSE.
wire clk_sys, clk_unused, clk_unused_vid, locked;
wire clk_vid = CLK_AUDIO;
wire core_reset = RESET | ~locked;
pll pll (
    .refclk(CLK_50M), .rst(1'b0), .outclk_0(clk_sys),
    .outclk_1(clk_unused), .outclk_2(clk_unused_vid), .locked(locked)
);
assign CLK_VIDEO = clk_vid;
assign DDRAM_CLK = clk_sys;
wire ce_pix;
wire reset_sys_active, reset_vid_active;
tic80_pixel_enable pixel_enable (.clk(clk_vid), .reset(reset_vid_active), .ce_pix(ce_pix));
assign CE_PIXEL = ce_pix;
assign NATIVE_VID_ACTIVE = 1'b1;
assign VGA_F1 = 0;
assign VGA_SL = 0;
assign VGA_SCALER = 0;
assign VGA_DISABLE = 0;
assign HDMI_FREEZE = 0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;
assign AUDIO_S = 1;
assign AUDIO_MIX = 0;
assign ADC_BUS = 'z;
assign {SD_SCK, SD_MOSI, SD_CS} = 'z;
assign {UART_RTS, UART_DTR} = 0;
assign UART_TXD = UART_RXD;
assign USER_OUT = '1;
assign BUTTONS = 0;
assign LED_DISK = 0;
assign LED_POWER = 2'b01;
assign LED_USER = video_active;
assign SDRAM_CLK = 0;
assign SDRAM_CKE = 0;
assign SDRAM_A = 0;
assign SDRAM_BA = 0;
assign SDRAM_DQ = 'z;
assign {SDRAM_DQML, SDRAM_DQMH} = 2'b11;
assign {SDRAM_nCS, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nWE} = 4'b1111;
`ifdef MISTER_DUAL_SDRAM
assign SDRAM2_CLK = 'z;
assign SDRAM2_A = 'z;
assign SDRAM2_BA = 'z;
assign SDRAM2_DQ = 'z;
assign {SDRAM2_nCS, SDRAM2_nCAS, SDRAM2_nRAS, SDRAM2_nWE} = 'z;
`endif
`ifdef MISTER_FB
assign FB_EN = 0;
assign FB_FORMAT = 0;
assign FB_WIDTH = 0;
assign FB_HEIGHT = 0;
assign FB_BASE = 0;
assign FB_STRIDE = 0;
assign FB_FORCE_BLANK = 0;
`ifdef MISTER_FB_PALETTE
assign FB_PAL_CLK = 0;
assign FB_PAL_ADDR = 0;
assign FB_PAL_DOUT = 0;
assign FB_PAL_WR = 0;
`endif
`endif

`include "build_id.v"
localparam CONF_STR = {
    "TIC-80;;",
    "F0,TICPNG,Load Cartridge;",
    "-;",
    "O12,Aspect Ratio,Original,Full Screen,[ARC1],[ARC2];",
    "R0,Reset Cartridge;",
    "J,Z,X,A,S,W (Up),A (Left),S (Down),D (Right),Enter,Esc,Q,E;",
    "jn,A,B,X,Y,,,,,Start,Select,L,R;",
    "jp,A,B,X,Y,,,,,Start,Select,L,R;",
    "V,v",`BUILD_DATE
};
wire [127:0] status;
wire [1:0] hps_buttons;
wire [10:0] ps2_key;
wire [24:0] ps2_mouse;
wire [15:0] ps2_mouse_ext;
wire [31:0] joystick_0, joystick_1, joystick_2, joystick_3;
wire ioctl_download, ioctl_wr, ioctl_wait;
wire [26:0] ioctl_addr;
wire [7:0] ioctl_dout;
wire [15:0] ioctl_index;
wire [35:0] extension_bus;
wire [31:0] horizontal_wheel;
// This extension only consumes UIO words; generic hps_io retains all replies.
assign extension_bus[32] = 1'b0;
assign extension_bus[15:0] = 16'd0;
tic80_horizontal_wheel horizontal_input (
    .clk(clk_sys), .reset(reset_sys_active), .osd_open(OSD_STATUS),
    .io_enable(extension_bus[34]), .io_strobe(extension_bus[33]),
    .io_din(extension_bus[31:16]), .total(horizontal_wheel)
);
// Pass F12 to cartridges; Win+F12 retains access to the MiSTer menu.
hps_io #(.CONF_STR(CONF_STR), .VDNUM(1), .F12KEYMOD(1)) hps_io (
    .clk_sys(clk_sys), .HPS_BUS(HPS_BUS), .EXT_BUS(extension_bus), .status(status),
    .buttons(hps_buttons),
    .ps2_key(ps2_key), .ps2_mouse(ps2_mouse), .ps2_mouse_ext(ps2_mouse_ext),
    .joystick_0(joystick_0), .joystick_1(joystick_1),
    .joystick_2(joystick_2), .joystick_3(joystick_3),
    .ioctl_download(ioctl_download), .ioctl_wr(ioctl_wr), .ioctl_addr(ioctl_addr),
    .ioctl_dout(ioctl_dout), .ioctl_index(ioctl_index), .ioctl_wait(ioctl_wait),
    .status_menumask(16'd0),
    .sd_lba('{32'd0}), .sd_rd(1'b0), .sd_wr(1'b0), .sd_buff_din('{8'd0})
);
// The 256x144 image remains centered in the CRT carrier. DE covers only that
// image so HDMI captures it at its native aspect ratio and fills a 16:9 display.
wire [1:0] aspect = status[2:1];
assign VIDEO_ARX = aspect == 0 ? 13'd16 : {11'd0, aspect - 2'd1};
assign VIDEO_ARY = aspect == 0 ? 13'd9 : 13'd0;
wire video_active;
tic80_video_top video (
    .clk_sys(clk_sys), .clk_vid(clk_vid), .clk_audio(CLK_AUDIO), .ce_pix(ce_pix), .reset(core_reset),
    .reset_sys_active(reset_sys_active), .reset_vid_active(reset_vid_active),
    .joystick_0(joystick_0), .joystick_1(joystick_1),
    .joystick_2(joystick_2), .joystick_3(joystick_3),
    // MiSTer's keyboard/MGL reset uses buttons[1]; the OSD reset uses status[0].
    // Both restart the ARM cartridge while preserving its private data/save.
    .osd_status({status[31:1], status[0] | hps_buttons[1]}),
    .osd_open(OSD_STATUS), .ps2_key(ps2_key), .ps2_mouse(ps2_mouse), .ps2_mouse_ext(ps2_mouse_ext),
    .horizontal_wheel(horizontal_wheel),
    .ioctl_download(ioctl_download), .ioctl_wr(ioctl_wr),
    .ioctl_addr(ioctl_addr), .ioctl_dout(ioctl_dout), .ioctl_index(ioctl_index), .ioctl_wait(ioctl_wait),
    .ddr_busy(DDRAM_BUSY), .ddr_burstcnt(DDRAM_BURSTCNT),
    .ddr_addr(DDRAM_ADDR), .ddr_rd(DDRAM_RD), .ddr_dout(DDRAM_DOUT),
    .ddr_dout_ready(DDRAM_DOUT_READY), .ddr_din(DDRAM_DIN),
    .ddr_be(DDRAM_BE), .ddr_we(DDRAM_WE),
    .r(VGA_R), .g(VGA_G), .b(VGA_B), .hs(VGA_HS), .vs(VGA_VS),
    .de(VGA_DE), .active(video_active), .audio_l(AUDIO_L), .audio_r(AUDIO_R)
);
