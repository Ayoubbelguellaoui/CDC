module bm_gray_counter (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic [3:0]  count_in,
    output logic [3:0]  count_out
);
    logic [3:0] gray_in, gray_out;
    assign gray_in = count_in ^ (count_in >> 1);
    always_ff @(posedge clk_b) gray_out <= gray_in;
    assign count_out = gray_out ^ (gray_out >> 1);
endmodule
