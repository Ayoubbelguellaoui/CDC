`timescale 1ns/1ps

// Correct design: 2-phase (toggle) req/ack handshake carrying an 8-bit
// payload. Each new word toggles req_valid; the destination detects the
// toggle through a 2FF chain, captures the payload, and follows with
// dst_ack; the source observes the toggled-back ack through its own 2FF
// chain before launching the next word. No level handshake, no FIFO.
// [CORPUS NOTE — verified 2026-09-26] Control crossings are clean
// (CDC001 info/verified_safe), but the payload hs_data -> hs_capture is
// STILL reported CDC001 error + CDC002 error: handshake verification
// covers only the req_valid/dst_ack control pair, not the guarded data
// (same limitation as the 4-phase fixture — documented false positive).
// Naming note: the toggle is called req_valid because in a 2-phase
// protocol the request edge doubles as the validity event; the
// recognizer seeds handshake roles from valid/ready/ack naming.

module handshake_2phase_req_ack (
    input  wire       src_clk,
    input  wire       dst_clk,
    input  wire       rst_n,
    input  wire [7:0] payload_in,
    input  wire       send,
    output wire [7:0] payload_out,
    output wire       busy
);

    reg [7:0] hs_data;
    reg       req_valid;
    reg       req_valid_sync1;
    reg       req_valid_sync2;
    reg       dst_ack;
    reg       dst_ack_sync1;
    reg       dst_ack_sync2;
    reg [7:0] hs_capture;

    // Source: launch + toggle; wait until ack follows the toggle.
    always @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) begin
            hs_data <= 8'b0;
            req_valid <= 1'b0;
        end else if (send && (dst_ack_sync2 == req_valid)) begin
            hs_data <= payload_in;
            req_valid <= ~req_valid;
        end
    end

    // Request toggle synchronized into the destination domain.
    always @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            req_valid_sync1 <= 1'b0;
            req_valid_sync2 <= 1'b0;
        end else begin
            req_valid_sync1 <= req_valid;
            req_valid_sync2 <= req_valid_sync1;
        end
    end

    // Destination: on toggle, capture and follow with ack.
    always @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            hs_capture <= 8'b0;
            dst_ack    <= 1'b0;
        end else if (req_valid_sync2 != dst_ack) begin
            hs_capture <= hs_data;
            dst_ack    <= req_valid_sync2;
        end
    end

    // Ack synchronized back into the source domain.
    always @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) begin
            dst_ack_sync1 <= 1'b0;
            dst_ack_sync2 <= 1'b0;
        end else begin
            dst_ack_sync1 <= dst_ack;
            dst_ack_sync2 <= dst_ack_sync1;
        end
    end

    assign payload_out = hs_capture;
    assign busy = (dst_ack_sync2 != req_valid);

endmodule
