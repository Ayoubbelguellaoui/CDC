module wire_crossing (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    logic src_ff;
    logic mid_wire;
    logic dst_ff;

    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) src_ff <= 1'b0; else src_ff <= d;

    assign mid_wire = src_ff;

    always_ff @(posedge clk_b or negedge rst_n)
        if (!rst_n) dst_ff <= 1'b0; else dst_ff <= mid_wire;

    assign q = dst_ff;
endmodule
