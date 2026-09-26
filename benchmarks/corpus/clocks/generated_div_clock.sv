`timescale 1ns/1ps

// Generated-clock design: clk_div is a divide-by-4 of clk_core made with
// a local counter. A status word generated in the clk_div domain is
// consumed in the clk_core domain with no synchronizer.
// The divided clock is phase-related to its parent, but the sampling
// relationship is still a clock-domain crossing for this tool.

module generated_div_clock (
    input  wire       clk_core,
    input  wire       rst_n,
    input  wire [7:0] status_in,
    output wire [7:0] status_out
);

    reg [1:0] div_cnt;
    reg       clk_div;
    reg [7:0] genclk_src;
    reg [7:0] genclk_dst;

    always @(posedge clk_core or negedge rst_n) begin
        if (!rst_n) begin
            div_cnt <= 2'b0;
            clk_div <= 1'b0;
        end else if (div_cnt == 2'b11) begin
            div_cnt <= 2'b0;
            clk_div <= ~clk_div;
        end else begin
            div_cnt <= div_cnt + 1'b1;
        end
    end

    always @(posedge clk_div or negedge rst_n) begin
        if (!rst_n) genclk_src <= 8'b0;
        else genclk_src <= status_in;
    end

    // NOTE: clk_div is generated from clk_core, yet the transfer still
    // crosses an analyzed clock boundary with no synchronization.
    always @(posedge clk_core or negedge rst_n) begin
        if (!rst_n) genclk_dst <= 8'b0;
        else genclk_dst <= genclk_src;
    end

    assign status_out = genclk_dst;

endmodule
