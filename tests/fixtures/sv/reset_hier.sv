// Resets through the hierarchy, one clock. rh_rename: one reset net, named
// i_reset_n in one child and rst_n in the other - one reset domain, no
// CDC009. rh_two: two reset nets, both named rst_n inside the children (the
// top reset and its synchronized copy) - two reset domains, a CDC009.
module rh_ff_a (
    input  logic clk,
    input  logic i_reset_n,
    input  logic d,
    output logic q
);
    always_ff @(posedge clk or negedge i_reset_n)
        if (!i_reset_n) q <= 1'b0;
        else q <= d;
endmodule

module rh_ff_b (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) q <= 1'b0;
        else q <= d;
endmodule

module rh_rename (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    logic a_q;
    rh_ff_a u_a (.clk(clk), .i_reset_n(rst_n), .d(d), .q(a_q));
    rh_ff_b u_b (.clk(clk), .rst_n(rst_n), .d(a_q), .q(q));
endmodule

module rh_two (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    logic rst_meta, rst_sync, a_q;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) {rst_sync, rst_meta} <= 2'b00;
        else {rst_sync, rst_meta} <= {rst_meta, 1'b1};
    rh_ff_b u_a (.clk(clk), .rst_n(rst_n), .d(d), .q(a_q));
    rh_ff_b u_b (.clk(clk), .rst_n(rst_sync), .d(a_q), .q(q));
endmodule
