// Assignments to a concatenation. cl_sync: a 2FF written as one shift,
// `{s1, s0} <= {s0, a_r}` - a chain. cl_unsync: two registers loaded together
// from the other clock - both cross. cl_assign: `assign {c, s} = a_r + b_r`
// feeds a register of the other clock.
module cl_sync (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0, s1;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) {s1, s0} <= {s0, a_r};
    assign q = s1;
endmodule

module cl_unsync (input logic clk_a, input logic clk_b, input logic [1:0] d,
                  output logic [1:0] q);
    logic a0, a1, q0, q1;
    always_ff @(posedge clk_a) {a1, a0} <= d;
    always_ff @(posedge clk_b) {q1, q0} <= {a1, a0};
    assign q = {q1, q0};
endmodule

module cl_assign (input logic clk_a, input logic clk_b, input logic [3:0] d, input logic [3:0] e,
                  output logic [3:0] q);
    logic [3:0] a_r, b_r, s;
    logic c;
    always_ff @(posedge clk_a) begin
        a_r <= d;
        b_r <= e;
    end
    assign {c, s} = a_r + b_r;
    always_ff @(posedge clk_b) q <= s ^ {3'b0, c};
endmodule
