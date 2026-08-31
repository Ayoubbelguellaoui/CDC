`timescale 1ns/1ps

// CDC006 Benchmark: Combinational Logic Between Sync Stages
// Positive: cross-domain source feeds first stage of 2FF chain (MUST trigger CDC006)
// Negative: properly structured 2FF chain with single same-domain predecessor (MUST NOT trigger)

module cdc006_benchmark (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire data_in
);

    // ====================================================================
    // POSITIVE: Cross-domain source directly feeds first sync stage (MUST trigger)
    // ====================================================================
    reg positive_src;
    reg positive_meta;
    reg positive_sync;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) positive_src <= 1'b0;
        else positive_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            positive_meta <= 1'b0;
            positive_sync <= 1'b0;
        end else begin
            positive_meta <= positive_src;
            positive_sync <= positive_meta;
        end
    end

    // ====================================================================
    // NEGATIVE: Proper 2FF chain, single same-domain predecessor (MUST NOT trigger)
    // ====================================================================
    reg negative_src;
    reg negative_meta;
    reg negative_sync;
    reg negative_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) negative_src <= 1'b0;
        else negative_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            negative_meta <= 1'b0;
            negative_sync <= 1'b0;
        end else begin
            negative_meta <= negative_src;
            negative_sync <= negative_meta;
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) negative_dst <= 1'b0;
        else negative_dst <= negative_sync;
    end

    // ====================================================================
    // POSITIVE 2: Multi-predecessor second stage (MUST trigger CDC006)
    // ====================================================================
    reg pos2_src;
    reg pos2_other;
    reg pos2_meta;
    reg pos2_consumer;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) pos2_src <= 1'b0;
        else pos2_src <= data_in;
    end

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) pos2_other <= 1'b0;
        else pos2_other <= ~data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            pos2_meta <= 1'b0;
            pos2_consumer <= 1'b0;
        end else begin
            pos2_meta <= pos2_src;
            pos2_consumer <= pos2_meta | pos2_other;
        end
    end

endmodule
