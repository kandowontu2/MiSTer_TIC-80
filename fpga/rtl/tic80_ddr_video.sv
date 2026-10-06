// MiSTer DDR master for TIC-80 video and control words. Byte offsets come from
// memory_map.svh; the external bus addresses 64-bit words. Requests are held
// until !ddr_busy. Exactly one read transaction may be outstanding.
module tic80_ddr_video (
    input  logic clk,
    input  logic reset,
    input  logic vblank_start,
    input  logic [31:0] joystick_0, joystick_1, joystick_2, joystick_3,
    input logic [31:0] osd_status,
    input logic [511:0] input_keys,
    input logic [63:0] input_mouse,
    input logic [31:0] input_horizontal_wheel,
    input logic [63:0] cart_meta,
    output logic [31:0] cart_ack,
    output logic cart_ack_valid,
    input logic cart_write,
    input logic [28:0] cart_address,
    input logic [63:0] cart_data,
    input logic [7:0] cart_be,
    output logic cart_ready,
    input  logic ddr_busy,
    output logic [7:0] ddr_burstcnt,
    output logic [28:0] ddr_addr,
    output logic ddr_rd,
    input  logic [63:0] ddr_dout,
    input  logic ddr_dout_ready,
    input logic consumed_toggle,
    // Audio uses the same serialized DDR port. All these ports use clk.
    input logic audio_request,
    input logic [28:0] audio_address,
    output logic audio_ready,
    output logic [31:0] audio_write_counter,
    input logic [31:0] audio_read_counter,
    input logic [31:0] audio_samples, audio_underruns,
    output logic audio_session_reset,
    output logic audio_session_active,
    output logic [63:0] ddr_din,
    output logic [7:0] ddr_be,
    output logic ddr_we,
    // Writes into the inactive FPGA frame memory (64-bit words).
    output logic frame_write,
    output logic frame_write_bank,
    output logic [14:0] frame_write_address,
    output logic [63:0] frame_write_data,
    // Bank and toggle remain stable across the video-domain synchronization.
    output logic frame_bank,
    output logic frame_toggle,
    output logic source_valid
);
    `include "memory_map.svh"
    localparam logic [14:0] FRAME_WORDS = 15'(TM_FRAME_BYTES / 8);
    typedef enum logic [3:0] {POLL_REQUEST, WAIT_REQUEST, PREPARE_WRITE,
                             WRITE_STATUS, IDLE, DMA_REQUEST, DMA_WAIT,
                             AUDIO_REQUEST, AUDIO_WAIT, CART_WRITE} state_t;
    state_t state;
    logic [1:0] poll_index;
    logic [4:0] write_index;
    logic [31:0] input_sequence;
    logic [31:0] stats_sequence;
    logic [63:0] stats_snapshot;
    logic [31:0] cart_status_snapshot;
    logic stats_snapshot_valid;
    logic [511:0] keys_snapshot;
    logic [63:0] mouse_snapshot;
    logic [31:0] horizontal_snapshot;
    logic input_snapshot_valid;
    wire [2:0] input_word_index = 3'(write_index - 5'd16);
    logic [31:0] session_request, video_publish;
    logic [31:0] boot_session;
    logic baseline_valid, session_armed;
    logic [31:0] identity, geometry, heartbeat, session_ack, presented, front_address;
    logic session_reset;
    logic [28:0] write_address;
    logic [63:0] write_data;
    logic [31:0] copied_word, dma_word;
    logic [28:0] dma_base;
    logic [14:0] dma_index;
    logic [5:0] dma_beat;
    wire dma_busy = state == DMA_REQUEST || state == DMA_WAIT;
    assign audio_session_reset = session_reset;
    assign audio_session_active = session_ack != 0 && session_request == session_ack;
    assign audio_ready = state == AUDIO_WAIT && ddr_dout_ready && !reset;
    assign cart_ready = state == CART_WRITE && !ddr_busy && !reset;

    tic80_video_control control (
        .clk(clk), .reset(reset), .vblank_start(vblank_start),
        .reads_idle(!dma_busy), .session_request(session_request),
        .video_publish(video_publish), .identity(identity), .geometry(geometry),
        .heartbeat(heartbeat), .session_ack(session_ack),
        .video_presented(presented), .front_byte_address(front_address),
        .front_valid(source_valid), .session_reset(session_reset)
    );

    always_comb begin
        ddr_rd = 0;
        ddr_we = 0;
        ddr_addr = 0;
        ddr_burstcnt = 1;
        ddr_be = 8'hFF;
        ddr_din = write_data;
        if (state == POLL_REQUEST) begin
            ddr_rd = 1;
            ddr_addr = 29'((TM_PHYSICAL_BASE +
                (poll_index == 0 ? TM_SESSION_REQUEST_OFFSET :
                 poll_index == 1 ? TM_VIDEO_PUBLISH_OFFSET :
                 poll_index == 2 ? TM_AUDIO_WRITE_OFFSET : TM_CART_ACK_OFFSET)) >> 3);
        end else if (state == WRITE_STATUS) begin
            ddr_we = 1;
            ddr_addr = write_address;
        end else if (state == DMA_REQUEST) begin
            ddr_rd = 1;
            ddr_burstcnt = 64;
            ddr_addr = dma_base + {14'd0, dma_index};
        end else if (state == AUDIO_REQUEST) begin
            ddr_rd = 1;
            ddr_addr = audio_address;
        end else if (state == CART_WRITE) begin
            ddr_we = 1;
            ddr_addr = cart_address;
            ddr_din = cart_data;
            ddr_be = cart_be;
        end
        frame_write = state == DMA_WAIT && ddr_dout_ready;
        frame_write_bank = dma_word[0];
        frame_write_address = dma_index + {9'd0, dma_beat};
        frame_write_data = ddr_dout;
        if (reset) begin
            ddr_rd = 0;
            ddr_we = 0;
            frame_write = 0;
        end
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            state <= POLL_REQUEST;
            poll_index <= 0;
            audio_write_counter <= 0;
            cart_ack <= 0;
            cart_ack_valid <= 0;
            write_index <= 0;
            input_sequence <= 0;
            stats_sequence <= 0;
            stats_snapshot <= 0;
            cart_status_snapshot <= 0;
            stats_snapshot_valid <= 0;
            keys_snapshot <= 0;
            mouse_snapshot <= 0;
            horizontal_snapshot <= 0;
            input_snapshot_valid <= 0;
            write_address <= 0;
            write_data <= 0;
            session_request <= 0;
            video_publish <= 0;
            boot_session <= 0;
            baseline_valid <= 0;
            session_armed <= 0;
            copied_word <= 0;
            dma_word <= 0;
            dma_base <= 0;
            dma_index <= 0;
            dma_beat <= 0;
            frame_bank <= 0;
            frame_toggle <= 0;
        end else begin
            if (session_reset) copied_word <= 0;
            case (state)
                POLL_REQUEST: if (!ddr_busy) state <= WAIT_REQUEST;
                WAIT_REQUEST: if (ddr_dout_ready) begin
                    if (poll_index == 0) begin
                        // Capture stale DDR on boot without accepting its old
                        // process as a new live session. ARM must change nonce.
                        if (!baseline_valid) begin
                            boot_session <= ddr_dout[31:0];
                            baseline_valid <= 1;
                        end else if (session_armed || ddr_dout[31:0] != boot_session) begin
                            session_armed <= 1;
                            session_request <= ddr_dout[31:0];
                        end
                        poll_index <= 1;
                        state <= POLL_REQUEST;
                    end else if (poll_index == 1) begin
                        video_publish <= ddr_dout[31:0];
                        poll_index <= 2;
                        state <= POLL_REQUEST;
                    end else if (poll_index == 2) begin
                        audio_write_counter <= ddr_dout[31:0];
                        poll_index <= 3;
                        state <= POLL_REQUEST;
                    end else begin
                        cart_ack <= ddr_dout[31:0];
                        cart_ack_valid <= 1;
                        poll_index <= 0;
                        write_index <= 0;
                        state <= IDLE;
                    end
                end
                IDLE: begin
                    if (audio_request && !session_reset) state <= AUDIO_REQUEST;
                    else if (cart_write) state <= CART_WRITE;
                    else if (!session_reset && source_valid && session_request == session_ack &&
                        presented != copied_word && frame_toggle == consumed_toggle) begin
                        dma_word <= presented;
                        dma_base <= front_address[31:3];
                        dma_index <= 0;
                        dma_beat <= 0;
                        state <= DMA_REQUEST;
                    end else state <= PREPARE_WRITE;
                end
                PREPARE_WRITE: begin
                    // Snapshot the complete write command before asserting it.
                    // Heartbeat/input changes cannot alter a stalled command.
                    case (write_index)
                        0: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_IDENTITY_OFFSET) >> 3);
                            write_data <= {TM_CART_SOURCE_MAGIC, identity};
                        end
                        1: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_GEOMETRY_OFFSET) >> 3);
                            write_data <= {TM_LINUX_INPUT_MAGIC, geometry};
                        end
                        2: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_SESSION_ACK_OFFSET) >> 3);
                            write_data <= {32'd0, session_ack};
                        end
                        3: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_VIDEO_PRESENTED_OFFSET) >> 3);
                            // Delay the public acknowledgment until the complete
                            // source frame is safely in FPGA memory.
                            write_data <= {32'd0, source_valid ? copied_word : 32'd0};
                        end
                        4: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_HEARTBEAT_OFFSET) >> 3);
                            write_data <= {32'd0, heartbeat};
                        end
                        5: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_JOY0_OFFSET) >> 3);
                            write_data <= {32'd0, joystick_0};
                        end
                        6: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_JOY1_OFFSET) >> 3);
                            write_data <= {32'd0, joystick_1};
                        end
                        7: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_JOY2_OFFSET) >> 3);
                            write_data <= {32'd0, joystick_2};
                        end
                        8: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_JOY3_OFFSET) >> 3);
                            write_data <= {32'd0, joystick_3};
                        end
                        9: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_AUDIO_READ_OFFSET) >> 3);
                            write_data <= {32'd0, audio_read_counter};
                        end
                        10: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_CART_META_OFFSET) >> 3);
                            write_data <= cart_meta;
                            // Pair reset release with this exact cart metadata.
                            // DDR backpressure must not publish a new released
                            // reset beside an older, not-yet-ready cart ticket.
                            cart_status_snapshot <= osd_status;
                        end
                        11: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_STATUS_OFFSET) >> 3);
                            write_data <= {32'd0, cart_status_snapshot};
                        end
                        12: begin
                            stats_snapshot <= {audio_underruns, audio_samples};
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_STATS_SEQUENCE_OFFSET) >> 3);
                            write_data <= {TM_STATS_MAGIC, stats_sequence + 32'd1};
                        end
                        13: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_AUDIO_STATS_OFFSET) >> 3);
                            write_data <= stats_snapshot;
                        end
                        14: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_STATS_SEQUENCE_OFFSET) >> 3);
                            write_data <= {TM_STATS_MAGIC, stats_sequence + 32'd2};
                        end
                        15: begin
                            // Publish only changes; a continuous writer could
                            // starve ARM's coherent snapshot retry loop.
                            keys_snapshot <= input_keys;
                            mouse_snapshot <= input_mouse;
                            horizontal_snapshot <= input_horizontal_wheel;
                            input_snapshot_valid <= 1;
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_KEYBOARD_OFFSET) >> 3);
                            write_data <= {32'd0, input_sequence + 32'd1};
                        end
                        24: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_MOUSE_OFFSET) >> 3);
                            write_data <= mouse_snapshot;
                        end
                        25: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_HORIZONTAL_WHEEL_OFFSET) >> 3);
                            write_data <= {32'd0, horizontal_snapshot};
                        end
                        26: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_KEYBOARD_OFFSET) >> 3);
                            write_data <= {32'd0, input_sequence + 32'd2};
                        end
                        default: begin
                            write_address <= 29'((TM_PHYSICAL_BASE + TM_KEYBOARD_BITS_OFFSET) >> 3) + {26'd0, input_word_index};
                            write_data <= keys_snapshot[{input_word_index, 6'd0} +: 64];
                        end
                    endcase
                    state <= WRITE_STATUS;
                end
                WRITE_STATUS: if (!ddr_busy) begin
                    if (write_index == 11 && stats_snapshot_valid &&
                            stats_snapshot == {audio_underruns, audio_samples}) begin
                        if (input_snapshot_valid && input_keys == keys_snapshot && input_mouse == mouse_snapshot && input_horizontal_wheel == horizontal_snapshot)
                            state <= POLL_REQUEST;
                        else begin write_index <= 15; state <= PREPARE_WRITE; end
                    end else if (write_index == 14) begin
                        stats_sequence <= stats_sequence + 32'd2;
                        stats_snapshot_valid <= 1;
                        if (input_snapshot_valid && input_keys == keys_snapshot && input_mouse == mouse_snapshot && input_horizontal_wheel == horizontal_snapshot)
                            state <= POLL_REQUEST;
                        else begin write_index <= 15; state <= PREPARE_WRITE; end
                    end else if (write_index == 26) begin
                        input_sequence <= input_sequence + 32'd2;
                        state <= POLL_REQUEST;
                    end else begin
                        write_index <= write_index + 1'b1;
                        state <= PREPARE_WRITE;
                    end
                end
                DMA_REQUEST: if (!ddr_busy) state <= DMA_WAIT;
                AUDIO_REQUEST: if (!ddr_busy) state <= AUDIO_WAIT;
                AUDIO_WAIT: if (ddr_dout_ready) state <= IDLE;
                CART_WRITE: if (!ddr_busy) state <= IDLE;
                DMA_WAIT: if (ddr_dout_ready) begin
                    if (dma_beat == 63) begin
                        dma_beat <= 0;
                        if (dma_index == FRAME_WORDS - 15'd64) begin
                            copied_word <= dma_word;
                            frame_bank <= dma_word[0];
                            frame_toggle <= !frame_toggle;
                            state <= IDLE;
                        end else begin
                            dma_index <= dma_index + 15'd64;
                            state <= DMA_REQUEST;
                        end
                    end else dma_beat <= dma_beat + 1'b1;
                end
                default: state <= POLL_REQUEST;
            endcase
        end
    end
endmodule
