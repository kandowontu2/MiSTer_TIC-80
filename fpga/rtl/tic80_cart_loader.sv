// Bounded byte-wide MiSTer file download. CART_META low32 packs a 30-bit
// sequence plus state (0 empty, 1 receiving, 2 ready, 3 error); high32 is size.
// Staging cannot be reused until ARM acknowledges the complete ticket.
module tic80_cart_loader (
    input logic clk, reset,
    input logic ioctl_download, ioctl_wr,
    input logic [26:0] ioctl_addr,
    input logic [7:0] ioctl_dout,
    input logic [15:0] ioctl_index,
    output logic ioctl_wait,
    input logic [31:0] cart_ack,
    input logic cart_ack_valid,
    output logic [63:0] cart_meta,
    output logic write_request,
    output logic [28:0] write_address,
    output logic [63:0] write_data,
    output logic [7:0] write_be,
    input logic write_ready
);
    `include "memory_map.svh"
    typedef enum logic [2:0] {WAIT_START, RECEIVE, FINISH, RECEIVE_SOURCE, FINISH_SOURCE, PUBLISH} state_t;
    state_t state;
    logic [29:0] sequence_number;
    logic [22:0] byte_count;
    logic [63:0] assembled;
    logic [7:0] assembled_be;
    logic [1:0] meta_state;
    logic failed;
    logic source_pending, source_for_cart;
    logic [31:0] source_magic;
    logic [15:0] source_length, cart_source_length;
    logic [7:0] source_index;
    logic [7:0] cart_index;
    // Main_MiSTer packs the extension's ordinal into bits 7:6 of the F-slot
    // index: F0/TIC is 0x00 and F0/PNG is 0x40. Other file slots stay ignored.
    wire cart_selected = ioctl_index == 16'h0000 || ioctl_index == 16'h0040;
    wire source_selected = ioctl_index == 16'h00fe;
    wire pending_ack = (meta_state == 2 || meta_state == 3) && cart_ack != cart_meta[31:0];
    assign cart_meta = {9'd0, byte_count, sequence_number, meta_state};
    // Keep the next FIO command stalled after download closes until the final
    // DDR/source publication drains. Main must not release its MGL reset hold
    // while the selected cart is still only an uncommitted byte prefix.
    assign ioctl_wait = !reset && (cart_selected || source_selected) &&
        ((ioctl_download && state == WAIT_START && (!cart_ack_valid || pending_ack)) ||
         state == FINISH || state == FINISH_SOURCE || state == PUBLISH || write_request);
    always_ff @(posedge clk) begin
        if (reset) begin
            state <= WAIT_START;
            sequence_number <= 0;
            meta_state <= 0;
            byte_count <= 0;
            assembled <= 0;
            assembled_be <= 0;
            write_request <= 0;
            write_address <= 0;
            write_data <= 0;
            write_be <= 0;
            failed <= 0;
            source_pending <= 0;
            source_for_cart <= 0;
            source_magic <= 0;
            source_length <= 0;
            cart_source_length <= 0;
            source_index <= 0;
            cart_index <= 0;
        end else begin
            if (write_ready) write_request <= 0;
            case (state)
                WAIT_START: if (ioctl_download) begin
                    if ((cart_selected || source_selected) && cart_ack_valid && !pending_ack) begin
                        byte_count <= 0;
                        assembled <= 0;
                        assembled_be <= 0;
                        failed <= 0;
                        if (source_selected) begin
                            source_pending <= 0;
                            source_magic <= 0;
                            source_length <= 0;
                            state <= RECEIVE_SOURCE;
                        end else begin
                            sequence_number <= cart_ack[31:2] + 30'd1;
                            meta_state <= 1;
                            cart_index <= ioctl_index[7:0];
                            source_for_cart <= source_pending && source_index == ioctl_index[7:0];
                            cart_source_length <= source_length;
                            source_pending <= 0;
                            state <= RECEIVE;
                        end
                    end else if (!cart_selected && !source_selected) source_pending <= 0;
                end
                RECEIVE: begin
                    // Main restarts can leave download asserted. Selecting a
                    // different file index aborts the old cart even if no new
                    // data arrives before the stop command; never publish a
                    // valid-looking prefix as a complete cartridge.
                    if (ioctl_download && ioctl_index != {8'd0,cart_index}) failed <= 1;
                    if (ioctl_wr && ioctl_download && !failed) begin
                        if (write_request || ioctl_index != {8'd0,cart_index} || ioctl_addr != {4'd0, byte_count} ||
                                {9'd0, byte_count} >= TM_CART_CAPACITY) failed <= 1;
                        else begin
                            assembled[{byte_count[2:0], 3'd0} +: 8] <= ioctl_dout;
                            assembled_be[byte_count[2:0]] <= 1;
                            byte_count <= byte_count + 23'd1;
                            if (byte_count[2:0] == 7) begin
                                write_request <= 1;
                                write_address <= 29'((TM_PHYSICAL_BASE + TM_CART_DATA_OFFSET) >> 3) + {9'd0, byte_count[22:3]};
                                write_data <= {ioctl_dout, assembled[55:0]};
                                write_be <= 8'hFF;
                                assembled_be <= 0;
                            end
                        end
                    end
                    if (!ioctl_download) state <= FINISH;
                end
                FINISH: if (!write_request) begin
                    if (assembled_be != 0 && !failed) begin
                        write_request <= 1;
                        write_address <= 29'((TM_PHYSICAL_BASE + TM_CART_DATA_OFFSET) >> 3) + {9'd0, byte_count[22:3]};
                        write_data <= assembled;
                        write_be <= assembled_be;
                        assembled_be <= 0;
                    end else begin
                        // Publish the source tag before making this cart ready.
                        // It shares staging ownership and the exact cart ticket.
                        write_request <= 1;
                        write_address <= 29'((TM_PHYSICAL_BASE + TM_CART_SOURCE_OFFSET) >> 3);
                        write_data <= {16'd0, (source_for_cart && !failed ? cart_source_length : 16'd0),
                            sequence_number, (failed || byte_count < 4 ? 2'd3 : 2'd2)};
                        write_be <= 8'hFF;
                        state <= PUBLISH;
                    end
                end
                PUBLISH: if (!write_request) begin
                    meta_state <= failed || byte_count < 4 ? 2'd3 : 2'd2;
                    state <= WAIT_START;
                end
                RECEIVE_SOURCE: begin
                    if (ioctl_wr && ioctl_download && !failed) begin
                        if (write_request || !source_selected || ioctl_addr != {4'd0, byte_count} ||
                            byte_count >= 23'(TM_CART_SOURCE_CAPACITY + 8)) failed <= 1;
                        else begin
                            byte_count <= byte_count + 23'd1;
                            case (byte_count)
                                0,1,2,3: source_magic[{byte_count[1:0],3'd0} +: 8] <= ioctl_dout;
                                4: source_index <= ioctl_dout;
                                5: if (ioctl_dout != 0) failed <= 1;
                                6: source_length[7:0] <= ioctl_dout;
                                7: source_length[15:8] <= ioctl_dout;
                                default: begin
                                    if (source_magic != TM_CART_SOURCE_MAGIC ||
                                        (source_index != 0 && source_index != 8'h40) ||
                                        source_length < 2 || source_length > 16'(TM_CART_SOURCE_CAPACITY) ||
                                        byte_count >= {7'd0,source_length} + 23'd8 ||
                                        (byte_count == 8 && ioctl_dout != 8'h2f) ||
                                        ((ioctl_dout == 0) != (byte_count == {7'd0,source_length} + 23'd7))) failed <= 1;
                                    else begin
                                        assembled[{byte_count[2:0],3'd0} +: 8] <= ioctl_dout;
                                        assembled_be[byte_count[2:0]] <= 1;
                                        if (byte_count[2:0] == 7) begin
                                            write_request <= 1;
                                            write_address <= 29'((TM_PHYSICAL_BASE + TM_CART_SOURCE_OFFSET) >> 3) + {9'd0,byte_count[22:3]};
                                            write_data <= {ioctl_dout,assembled[55:0]};
                                            write_be <= 8'hFF;
                                            assembled_be <= 0;
                                        end
                                    end
                                end
                            endcase
                        end
                    end
                    if (!ioctl_download) state <= FINISH_SOURCE;
                end
                FINISH_SOURCE: if (!write_request) begin
                    if (assembled_be != 0 && !failed) begin
                        write_request <= 1;
                        write_address <= 29'((TM_PHYSICAL_BASE + TM_CART_SOURCE_OFFSET) >> 3) + {9'd0,byte_count[22:3]};
                        write_data <= assembled;
                        write_be <= assembled_be;
                        assembled_be <= 0;
                    end else begin
                        source_pending <= !failed && source_magic == TM_CART_SOURCE_MAGIC &&
                            source_length >= 2 && source_length <= 16'(TM_CART_SOURCE_CAPACITY) &&
                            byte_count == {7'd0,source_length} + 23'd8;
                        state <= WAIT_START;
                    end
                end
                default: state <= WAIT_START;
            endcase
        end
    end
endmodule
