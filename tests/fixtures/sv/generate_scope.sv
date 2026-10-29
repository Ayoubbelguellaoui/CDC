// Generate-loop scoping: every loop iteration is its own copy (g[0]..g[3]),
// and the loop reads the module's clocks and signals - the instance clock
// i_clk is clk_a of the module, not a domain of its own, and b_q reads the
// module register a_m: one unsynchronized clk_a -> clk_b crossing per
// iteration.
module gen_scope_ff (
    input  logic i_clk,
    input  logic d,
    output logic q
);
    always_ff @(posedge i_clk) q <= d;
endmodule

module generate_scope (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic [3:0] d,
    output logic [3:0] q
);
    logic [3:0] a_q;
    logic [3:0] a_m;
    always_ff @(posedge clk_a) a_m <= d;
    for (genvar i = 0; i < 4; i++) begin : g
        gen_scope_ff u_a (.i_clk(clk_a), .d(d[i]), .q(a_q[i]));
        logic b_q;
        always_ff @(posedge clk_b) b_q <= a_m[i];
        assign q[i] = b_q ^ a_q[i];
    end
endmodule
