`timescale 1ns/1ps

// CDC008 Benchmark: Multi-domain Daisy Chain
// Positive: signal crosses 3+ domains sequentially (MUST trigger CDC008)
// Negative: signal crosses only 2 domains (MUST NOT trigger CDC008)

module cdc008_benchmark (
    input  wire clk_a,
    input  wire clk_b,
    input  wire clk_c,
    input  wire rst_n,
    input  wire data_in
);

    // ====================================================================
    // POSITIVE: Signal crosses clk_a -> clk_b -> clk_c (MUST trigger CDC008)
    // ====================================================================
    reg positive_src;
    reg positive_mid;
    reg positive_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) positive_src <= 1'b0;
        else positive_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) positive_mid <= 1'b0;
        else positive_mid <= positive_src;
    end

    always @(posedge clk_c or negedge rst_n) begin
        if (!rst_n) positive_dst <= 1'b0;
        else positive_dst <= positive_mid;
    end

    // ====================================================================
    // NEGATIVE: Signal crosses only 2 domains (MUST NOT trigger CDC008)
    // ====================================================================
    reg negative_src;
    reg negative_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) negative_src <= 1'b0;
        else negative_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) negative_dst <= 1'b0;
        else negative_dst <= negative_src;
    end

endmodule
