`timescale 1ns/1ps

// CDC005 Benchmark: Muxed Clock No Reset
// Positive: register clocked by muxed clock without reset (MUST trigger CDC005)
// Negative: muxed clock WITH reset (MUST NOT trigger CDC005)

module cdc005_benchmark (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire sel,
    input  wire data_in
);

    wire clk_muxed;
    assign clk_muxed = sel ? clk_b : clk_a;

    // ====================================================================
    // POSITIVE: Muxed clock, no reset (MUST trigger CDC005)
    // ====================================================================
    reg positive_src;
    reg positive_dst;

    always @(posedge clk_muxed) begin
        positive_src <= data_in;
    end

    reg clk_cross;
    always @(posedge clk_cross or negedge rst_n) begin
        if (!rst_n) positive_dst <= 1'b0;
        else positive_dst <= positive_src;
    end

    // ====================================================================
    // NEGATIVE: Muxed clock, WITH reset (MUST NOT trigger CDC005)
    // ====================================================================
    reg negative_src;
    reg negative_dst;

    always @(posedge clk_muxed or negedge rst_n) begin
        if (!rst_n) negative_src <= 1'b0;
        else negative_src <= data_in;
    end

    always @(posedge clk_cross or negedge rst_n) begin
        if (!rst_n) negative_dst <= 1'b0;
        else negative_dst <= negative_src;
    end

endmodule
