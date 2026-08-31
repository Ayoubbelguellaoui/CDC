`timescale 1ns/1ps

// CDC007 Benchmark: Missing Reset
// Positive: CDC registers without reset (MUST trigger CDC007)
// Negative: CDC registers with reset (MUST NOT trigger CDC007)

module cdc007_benchmark (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire data_in
);

    // ====================================================================
    // POSITIVE: Both registers lack reset (MUST trigger CDC007)
    // ====================================================================
    reg positive_src;
    reg positive_dst;

    always @(posedge clk_a) begin
        positive_src <= data_in;
    end

    always @(posedge clk_b) begin
        positive_dst <= positive_src;
    end

    // ====================================================================
    // NEGATIVE: Both registers have reset (MUST NOT trigger CDC007)
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

    // ====================================================================
    // AMBIGUOUS: Only source has reset (documented behavior)
    // ====================================================================
    reg amb_src;
    reg amb_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) amb_src <= 1'b0;
        else amb_src <= data_in;
    end

    always @(posedge clk_b) begin
        amb_dst <= amb_src;
    end

endmodule
