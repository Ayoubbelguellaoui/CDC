// Signals leaving an instance. op_cross: a child register drives a parent
// register of another clock through the child's output port (an output
// connection is the Assignment `parent = <port>`: it was dropped).
// op_cgate: a clock gate instance, clk_o = en_latch & clk_i - its output
// is clk_i gated, not a root clock of its own. op_diamonds: 7 four-way
// diamonds between two clocks, 4^7 = 16384 simple paths to one register.
module op_ff (
    input  logic i_clk,
    input  logic d,
    output logic q
);
    always_ff @(posedge i_clk) q <= d;
endmodule

module op_cross (
    input  logic clk_a,
    input  logic clk_b,
    input  logic d,
    output logic q
);
    logic a_q;
    op_ff u_a (.i_clk(clk_a), .d(d), .q(a_q));
    always_ff @(posedge clk_b) q <= a_q;
endmodule

module op_clock_gate (
    input  logic clk_i,
    input  logic en_i,
    output logic clk_o
);
    logic en_latch;
    always_latch begin
        if (!clk_i) en_latch = en_i;
    end
    assign clk_o = en_latch & clk_i;
endmodule

module op_cgate (
    input  logic clk_i,
    input  logic en,
    input  logic d,
    output logic q
);
    logic clk;
    logic a_r;
    op_clock_gate u_cg (.clk_i(clk_i), .en_i(en), .clk_o(clk));
    always_ff @(posedge clk_i) a_r <= d;
    always_ff @(posedge clk) q <= a_r;
endmodule

module op_diamonds (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic [3:0] d,
    output logic       q
);
    logic a_r;
    logic w[8];
    always_ff @(posedge clk_a) a_r <= d[0];
    assign w[0] = a_r;
    for (genvar i = 0; i < 7; i++) begin : g
        logic p0, p1, p2, p3;
        assign p0 = w[i] & d[0];
        assign p1 = w[i] | d[1];
        assign p2 = w[i] ^ d[2];
        assign p3 = w[i] & d[3];
        assign w[i+1] = p0 ^ p1 ^ p2 ^ p3;
    end
    always_ff @(posedge clk_b) q <= w[7];
endmodule
