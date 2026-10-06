// Frame ownership/session controller. All ports are in the DDR clock domain.
// The DDR bus adapter must supply synchronized vblank pulses and must assert
// reads_idle only once all reads using the previous front address have ended.
module tic80_video_control (
    input  logic        clk,
    input  logic        reset,
    input  logic        vblank_start,
    input  logic        reads_idle,
    input  logic [31:0] session_request,
    input  logic [31:0] video_publish,
    output logic [31:0] identity,
    output logic [31:0] geometry,
    output logic [31:0] heartbeat,
    output logic [31:0] session_ack,
    output logic [31:0] video_presented,
    output logic [31:0] front_byte_address,
    output logic        front_valid,
    output logic        session_reset
);
    `include "memory_map.svh"
    assign identity = TM_MAGIC;
    assign geometry = (TM_HEIGHT << 16) | TM_WIDTH;
    assign front_valid = video_presented[1];
    assign front_byte_address = TM_PHYSICAL_BASE +
        (video_presented[0] ? TM_BUFFER1_OFFSET : TM_BUFFER0_OFFSET);

    always_ff @(posedge clk) begin
        if (reset) begin
            heartbeat <= 0;
            session_ack <= 0;
            video_presented <= 0;
            session_reset <= 0;
        end else begin
            session_reset <= 0;
            if (vblank_start) heartbeat <= heartbeat + 32'd1;
            if (session_request != session_ack) begin
                // Producer has stopped publication and is requesting a flush.
                // Do not acknowledge while an old-frame read remains in flight.
                if (reads_idle) begin
                    video_presented <= 0;
                    session_ack <= session_request;
                    session_reset <= 1;
                end
            end else if (session_ack != 0 && vblank_start && reads_idle && video_publish[1]) begin
                video_presented <= video_publish;
            end
        end
    end
endmodule
