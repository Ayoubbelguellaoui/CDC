`timescale 1ns/1ps

// CDC003 Benchmark: Reconvergence Hazard
// Positive: multi-bit source fans out and reconverges (MUST trigger CDC003)
// Negative: single-bit source fans out (MUST NOT trigger CDC003 - not hazardous)
// Negative: synced paths that reconverge (MUST NOT trigger CDC003)
// Ambiguous: 3+ path fanout (documented: only 2-path checked)

module cdc003_benchmark (
    input  wire        clk_a,
    input  wire        clk_b,
    input  wire        rst_n,
    input  wire [7:0]  data_in,
    input  wire        single_bit
);

    // ====================================================================
    // POSITIVE CONTROL: Multi-bit source, 2 paths, reconverge (MUST trigger)
    // ====================================================================
    reg [7:0] pos_src;
    reg       pos_dst1;
    reg       pos_dst2;
    reg       pos_consumer;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) pos_src <= 8'b0;
        else pos_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            pos_dst1 <= 1'b0;
            pos_dst2 <= 1'b0;
        end else begin
            pos_dst1 <= pos_src[0];
            pos_dst2 <= pos_src[1];
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) pos_consumer <= 1'b0;
        else pos_consumer <= pos_dst1 & pos_dst2;
    end

    // ====================================================================
    // NEGATIVE CONTROL 1: Single-bit source (MUST NOT trigger - not hazardous)
    // ====================================================================
    reg       neg_single_src;
    reg       neg_single_dst1;
    reg       neg_single_dst2;
    reg       neg_single_consumer;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) neg_single_src <= 1'b0;
        else neg_single_src <= single_bit;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            neg_single_dst1 <= 1'b0;
            neg_single_dst2 <= 1'b0;
        end else begin
            neg_single_dst1 <= neg_single_src;
            neg_single_dst2 <= neg_single_src;
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) neg_single_consumer <= 1'b0;
        else neg_single_consumer <= neg_single_dst1 & neg_single_dst2;
    end

    // ====================================================================
    // NEGATIVE CONTROL 2: Synced paths reconverge (MUST NOT trigger)
    // ====================================================================
    reg [7:0] neg_sync_src;
    reg       neg_sync_meta1, neg_sync_sync1;
    reg       neg_sync_meta2, neg_sync_sync2;
    reg       neg_sync_consumer;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) neg_sync_src <= 8'b0;
        else neg_sync_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            neg_sync_meta1 <= 1'b0;
            neg_sync_sync1 <= 1'b0;
            neg_sync_meta2 <= 1'b0;
            neg_sync_sync2 <= 1'b0;
        end else begin
            neg_sync_meta1 <= neg_sync_src[0];
            neg_sync_sync1 <= neg_sync_meta1;
            neg_sync_meta2 <= neg_sync_src[1];
            neg_sync_sync2 <= neg_sync_meta2;
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) neg_sync_consumer <= 1'b0;
        else neg_sync_consumer <= neg_sync_sync1 & neg_sync_sync2;
    end

    // ====================================================================
    // AMBIGUOUS: 3-path fanout reconvergence
    // Expected: CDC003 may only detect 2 of the 3 paths
    // Documented: Current detector checks pairs, not all combinations
    // ====================================================================
    reg [7:0] amb_src;
    reg       amb_d1, amb_d2, amb_d3;
    reg       amb_consumer;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) amb_src <= 8'b0;
        else amb_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            amb_d1 <= 1'b0;
            amb_d2 <= 1'b0;
            amb_d3 <= 1'b0;
        end else begin
            amb_d1 <= amb_src[0];
            amb_d2 <= amb_src[1];
            amb_d3 <= amb_src[2];
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) amb_consumer <= 1'b0;
        else amb_consumer <= amb_d1 & amb_d2 & amb_d3;
    end

endmodule
