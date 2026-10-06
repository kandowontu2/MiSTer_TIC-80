module ddr_terminator_write #(parameter WIDTH = 64) (
    input logic clk, reset, busy, core_write,
    input logic [7:0] core_burst,
    output logic write_request, owns_bus,
    output logic [28:0] address,
    output logic [7:0] burst,
    output logic [15:0] byteenable
);
    localparam AW = 32-$clog2(WIDTH/8);
    wire [AW-1:0] master_address;
    wire [WIDTH/8-1:0] master_be;
    f2sdram_safe_terminator #(WIDTH, 8) terminator (
        .clk(clk), .rst_req_sync(reset),
        .waitrequest_master(busy), .burstcount_master(burst),
        .address_master(master_address), .readdata_master({WIDTH{1'b0}}),
        .readdatavalid_master(1'b0), .read_master(),
        .writedata_master(), .byteenable_master(master_be), .write_master(write_request),
        .waitrequest_slave(), .burstcount_slave(core_burst),
        .address_slave(AW'(29'h100)), .readdata_slave(), .readdatavalid_slave(),
        .read_slave(1'b0), .writedata_slave({WIDTH{1'b1}}),
        .byteenable_slave({WIDTH/8{1'b1}}), .write_slave(core_write)
    );
    assign address = 29'(master_address);
    assign byteenable = 16'(master_be);
    assign owns_bus = terminator.terminating;
endmodule
