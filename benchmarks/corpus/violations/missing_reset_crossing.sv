`timescale 1ns/1ps

// Known violation (CDC007): a properly 2FF-synchronized crossing where
// NEITHER side has a reset. The synchronizer itself is structurally
// fine, so CDC001 is satisfied, but both registers power up undefined.

module missing_reset_crossing (
    input  wire clk_a,
    input  wire clk_b,
    input  wire data_in,
    output wire data_out
);

    reg noreset_src;
    reg noreset_meta;
    reg noreset_sync;

    always @(posedge clk_a) begin
        noreset_src <= data_in;
    end

    always @(posedge clk_b) begin
        noreset_meta <= noreset_src;
        noreset_sync <= noreset_meta;
    end

    assign data_out = noreset_sync;

endmodule
