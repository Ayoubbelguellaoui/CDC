`include "toggle_width.svh"

module ifdef_crossing (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic [`TOGGLE_WIDTH-1:0] d,
    output logic [`TOGGLE_WIDTH-1:0] q
);
`ifdef ENABLE_CDC
    logic [`TOGGLE_WIDTH-1:0] src_ff;
    logic [`TOGGLE_WIDTH-1:0] dst_ff;

    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) src_ff <= '0; else src_ff <= d;

    always_ff @(posedge clk_b or negedge rst_n)
        if (!rst_n) dst_ff <= '0; else dst_ff <= src_ff;

    assign q = dst_ff;
`else
    assign q = d;
`endif
endmodule
