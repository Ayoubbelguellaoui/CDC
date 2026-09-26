`timescale 1ns/1ps

// Known violation: single-bit level signal crosses from clk_a to clk_b
// with NO synchronizer at all.
// Expected: CDC001 error on clean_unsync_src -> clean_unsync_dst.

module unsync_single_bit (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire data_in,
    output wire data_out
);

    reg clean_unsync_src;
    reg clean_unsync_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) clean_unsync_src <= 1'b0;
        else clean_unsync_src <= data_in;
    end

    // BUG: direct sampling of an asynchronous signal, no sync stages.
    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) clean_unsync_dst <= 1'b0;
        else clean_unsync_dst <= clean_unsync_src;
    end

    assign data_out = clean_unsync_dst;

endmodule
