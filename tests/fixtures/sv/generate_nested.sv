// Nested generate-loop scoping: every (i, j) iteration is its own copy
// (g_o[i].g_i[j]), a signal of the outer loop read in the inner loop is the
// outer copy (g_o[i].o_r, not one per inner iteration), and the module clock
// reaches the instance clock two loops down. Each b_q (clk_b) reads o_r
// (clk_a): one unsynchronized crossing per (i, j).
module gen_nested_ff (
    input  logic i_clk,
    input  logic d,
    output logic q
);
    always_ff @(posedge i_clk) q <= d;
endmodule

module generate_nested (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic [1:0] d,
    output logic [5:0] q
);
    for (genvar i = 0; i < 2; i++) begin : g_o
        logic o_r;
        always_ff @(posedge clk_a) o_r <= d[i];
        for (genvar j = 0; j < 3; j++) begin : g_i
            logic a_q;
            logic b_q;
            gen_nested_ff u_ff (.i_clk(clk_a), .d(o_r), .q(a_q));
            always_ff @(posedge clk_b) b_q <= o_r;
            assign q[i * 3 + j] = a_q ^ b_q;
        end
    end
endmodule
