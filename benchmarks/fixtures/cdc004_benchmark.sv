`timescale 1ns/1ps

// CDC004 Benchmark: Gated Clock Crossing
// Positive: register clocked by gated clock crossing domains (MUST trigger CDC004)
// Negative: ungated clock crossing (MUST NOT trigger CDC004)

module cdc004_benchmark (
    input  wire clk_raw,
    input  wire rst_n,
    input  wire enable,
    input  wire data_in
);

    wire clk_gated;
    assign clk_gated = clk_raw & enable;

    // ====================================================================
    // POSITIVE: Gated clock drives register that crosses domain
    // ====================================================================
    reg positive_src;
    reg [7:0] positive_data_src;
    reg positive_dst;

    always @(posedge clk_gated or negedge rst_n) begin
        if (!rst_n) positive_src <= 1'b0;
        else positive_src <= data_in;
    end

    always @(posedge clk_gated or negedge rst_n) begin
        if (!rst_n) positive_data_src <= 8'b0;
        else positive_data_src <= 8'hAB;
    end

    reg clk_b_dummy;
    always @(posedge clk_b_dummy or negedge rst_n) begin
        if (!rst_n) positive_dst <= 1'b0;
        else positive_dst <= positive_src;
    end

    // ====================================================================
    // NEGATIVE: Ungated clock crossing (MUST NOT trigger CDC004)
    // ====================================================================
    reg negative_src;
    reg negative_dst;

    always @(posedge clk_raw or negedge rst_n) begin
        if (!rst_n) negative_src <= 1'b0;
        else negative_src <= data_in;
    end

    always @(posedge clk_b_dummy or negedge rst_n) begin
        if (!rst_n) negative_dst <= 1'b0;
        else negative_dst <= negative_src;
    end

endmodule
