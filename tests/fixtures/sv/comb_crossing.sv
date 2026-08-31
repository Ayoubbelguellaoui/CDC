module comb_crossing (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] src_reg;
    logic [7:0] comb_data;
    logic [7:0] dst_reg;

    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) src_reg <= 8'b0; else src_reg <= d;

    assign comb_data = src_reg ^ 8'hFF;

    always_ff @(posedge clk_b or negedge rst_n)
        if (!rst_n) dst_reg <= 8'b0; else dst_reg <= comb_data;

    assign q = dst_reg;
endmodule
