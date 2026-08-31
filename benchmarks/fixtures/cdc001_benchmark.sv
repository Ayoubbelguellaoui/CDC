`timescale 1ns/1ps

// CDC001 Benchmark: Unsynchronized Crossing
//
// BEHAVIOR DOCUMENTATION:
// CDC001 fires on EVERY register-to-register edge across different clock domains.
// The detected_sync field annotates whether a 2FF/3FF chain exists downstream,
// but the crossing is STILL reported. This is by design: the tool reports all
// crossings and lets the user evaluate whether the sync chain is sufficient.
//
// Positive controls: crossings that MUST trigger CDC001
// Negative controls: same-domain registers that MUST NOT trigger CDC001

module cdc001_benchmark (
    input  wire        clk_a,
    input  wire        clk_b,
    input  wire        rst_n,
    input  wire [7:0]  data_in,
    input  wire        pulse_in
);

    // ====================================================================
    // POSITIVE 1: Direct single-bit crossing (MUST trigger CDC001)
    // ====================================================================
    reg positive_single_src;
    reg positive_single_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) positive_single_src <= 1'b0;
        else positive_single_src <= pulse_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) positive_single_dst <= 1'b0;
        else positive_single_dst <= positive_single_src;
    end

    // ====================================================================
    // POSITIVE 2: Direct multi-bit crossing (MUST trigger CDC001)
    // ====================================================================
    reg [7:0] positive_multi_src;
    reg [7:0] positive_multi_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) positive_multi_src <= 8'b0;
        else positive_multi_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) positive_multi_dst <= 8'b0;
        else positive_multi_dst <= positive_multi_src;
    end

    // ====================================================================
    // POSITIVE 3: Crossing with sync chain (MUST trigger CDC001 with sync annotation)
    // CDC001 fires on src->meta edge. detected_sync should be TwoFF.
    // ====================================================================
    reg positive_sync_src;
    reg positive_sync_meta;
    reg positive_sync_sync;
    reg positive_sync_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) positive_sync_src <= 1'b0;
        else positive_sync_src <= pulse_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            positive_sync_meta <= 1'b0;
            positive_sync_sync <= 1'b0;
        end else begin
            positive_sync_meta <= positive_sync_src;
            positive_sync_sync <= positive_sync_meta;
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) positive_sync_dst <= 1'b0;
        else positive_sync_dst <= positive_sync_sync;
    end

    // ====================================================================
    // NEGATIVE 1: Same-domain register-to-register (MUST NOT trigger CDC001)
    // ====================================================================
    reg negative_same_src;
    reg negative_same_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) negative_same_src <= 1'b0;
        else negative_same_src <= pulse_in;
    end

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) negative_same_dst <= 1'b0;
        else negative_same_dst <= negative_same_src;
    end

    // ====================================================================
    // NEGATIVE 2: Port-to-register same domain (MUST NOT trigger CDC001)
    // ====================================================================
    reg negative_port_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) negative_port_dst <= 8'b0;
        else negative_port_dst <= data_in;
    end

    // ====================================================================
    // NEGATIVE 3: Another same-domain pair (MUST NOT trigger CDC001)
    // ====================================================================
    reg [3:0] negative_chain_a;
    reg [3:0] negative_chain_b;
    reg [3:0] negative_chain_c;

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            negative_chain_a <= 4'b0;
            negative_chain_b <= 4'b0;
            negative_chain_c <= 4'b0;
        end else begin
            negative_chain_a <= data_in[3:0];
            negative_chain_b <= negative_chain_a;
            negative_chain_c <= negative_chain_b;
        end
    end

endmodule
