module gated_crossing (
    input  logic clk_a, clk_b, en, rst_n,
    input  logic d,
    output logic q
);
    logic clk_b_en;
    assign clk_b_en = clk_b & en;

    logic src_ff, dst_ff;

    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) src_ff <= 1'b0; else src_ff <= d;

    always_ff @(posedge clk_b_en or negedge rst_n)
        if (!rst_n) dst_ff <= 1'b0; else dst_ff <= src_ff;

    assign q = dst_ff;
endmodule
