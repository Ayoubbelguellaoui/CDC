module bm_simple_crossing (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out
);
    logic [7:0] reg_a;
    always_ff @(posedge clk_a) reg_a <= data_in;
    assign data_out = reg_a;
endmodule
