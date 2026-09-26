`timescale 1ns/1ps

// Correct design: 4-phase valid/ready handshake carrying an 8-bit payload.
// [CORPUS NOTE — verified 2026-09-26] The control crossings are clean
// (CDC001 info/verified_safe on valid/ready sync chains), but the payload
// crossing hs_data -> hs_capture is STILL reported as CDC001 error +
// CDC002 error: handshake recognition only verifies the valid/ready
// control pair itself (PatternRecognizer::is_verified_safe_crossing), it
// does not extend safety to the payload the handshake guards. This is a
// correct design that the tool flags — a documented false-positive class
// (see manifest ambiguous controls + CDC002 known gap).

module handshake_4phase_valid_ready (
    input  wire       src_clk,
    input  wire       dst_clk,
    input  wire       rst_n,
    input  wire [7:0] payload_in,
    input  wire       send,
    output wire [7:0] payload_out,
    output wire       busy
);

    reg [7:0] hs_data;
    reg       hs_valid;
    reg       hs_valid_sync1;
    reg       hs_valid_sync2;
    reg       hs_ready;
    reg       hs_ready_sync1;
    reg       hs_ready_sync2;
    reg [7:0] hs_capture;

    // Source domain: launch data + valid, hold until ready returns.
    always @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) begin
            hs_data  <= 8'b0;
            hs_valid <= 1'b0;
        end else if (send && !hs_valid) begin
            hs_data  <= payload_in;
            hs_valid <= 1'b1;
        end else if (hs_valid && hs_ready_sync2) begin
            hs_valid <= 1'b0;
        end
    end

    // Valid synchronized into the destination domain.
    always @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            hs_valid_sync1 <= 1'b0;
            hs_valid_sync2 <= 1'b0;
        end else begin
            hs_valid_sync1 <= hs_valid;
            hs_valid_sync2 <= hs_valid_sync1;
        end
    end

    // Destination domain: capture on valid, answer with ready.
    always @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            hs_capture <= 8'b0;
            hs_ready   <= 1'b0;
        end else if (hs_valid_sync2 && !hs_ready) begin
            hs_capture <= hs_data;
            hs_ready   <= 1'b1;
        end else if (!hs_valid_sync2 && hs_ready) begin
            hs_ready <= 1'b0;
        end
    end

    // Ready synchronized back into the source domain.
    always @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) begin
            hs_ready_sync1 <= 1'b0;
            hs_ready_sync2 <= 1'b0;
        end else begin
            hs_ready_sync1 <= hs_ready;
            hs_ready_sync2 <= hs_ready_sync1;
        end
    end

    assign payload_out = hs_capture;
    assign busy = hs_valid;

endmodule
