// Adapt the platform's HDMI write interface to its complete Intel PLL
// reconfiguration IP. The reduced controller did not reconfigure the HDMI
// output after fitting it at Y32: live CTS stayed at 148.5 MHz for 720p/60.
// This replacement must still pass live clock and panel qualification.
module pll_cfg_hdmi #(
    parameter reconf_width = 64,
    parameter device_family = "Cyclone V"
) (
    input wire mgmt_clk,
    input wire mgmt_reset,
    output wire [reconf_width-1:0] reconfig_to_pll,
    input wire [reconf_width-1:0] reconfig_from_pll,
    output wire mgmt_waitrequest,
    input wire [5:0] mgmt_address,
    input wire mgmt_write,
    input wire [31:0] mgmt_writedata
);
    // Main only writes this interface. Preserve backpressure and the same
    // reset/conduit connections; let the complete IP handle physical mapping.
    pll_cfg #(.reconf_width(reconf_width), .WAIT_FOR_LOCK(0)) full_controller (
        .mgmt_clk(mgmt_clk), .mgmt_reset(mgmt_reset),
        .mgmt_waitrequest(mgmt_waitrequest),
        .mgmt_read(1'b0), .mgmt_readdata(),
        .mgmt_write(mgmt_write), .mgmt_address(mgmt_address),
        .mgmt_writedata(mgmt_writedata),
        .reconfig_to_pll(reconfig_to_pll),
        .reconfig_from_pll(reconfig_from_pll)
    );
endmodule
