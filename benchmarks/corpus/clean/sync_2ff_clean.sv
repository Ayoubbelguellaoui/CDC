`timescale 1ns/1ps

// Clean design: single-bit level crossing with a proper 2FF synchronizer
// and asynchronous resets on every register.
// Expected: CDC001 downgraded to info/verified_safe only; no error findings.

module sync_2ff_clean (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire data_in,
    output wire data_out
);

    reg src_ff;
    reg meta_ff;
    reg sync_ff;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) src_ff <= 1'b0;
        else src_ff <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta_ff <= 1'b0;
            sync_ff <= 1'b0;
        end else begin
            meta_ff <= src_ff;
            sync_ff <= meta_ff;
        end
    end

    assign data_out = sync_ff;

endmodule
