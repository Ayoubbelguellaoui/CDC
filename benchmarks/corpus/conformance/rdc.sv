`timescale 1ns/1ps

// RDC conformance, run with rdc.yaml (same-clock reset crossings checked).

module cfr_ff_a (input logic clk, input logic i_reset_n, input logic d, output logic q);
    always_ff @(posedge clk or negedge i_reset_n)
        if (!i_reset_n) q <= 1'b0;
        else q <= d;
endmodule

module cfr_ff_b (input logic clk, input logic rst_n, input logic d, output logic q);
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) q <= 1'b0;
        else q <= d;
endmodule

// One reset net under two port names: one reset domain, no crossing.
module cf_rst_renamed (input logic clk, input logic rst_n, input logic d, output logic q);
    logic a_q;
    cfr_ff_a u_a (.clk(clk), .i_reset_n(rst_n), .d(d), .q(a_q));
    cfr_ff_b u_b (.clk(clk), .rst_n(rst_n), .d(a_q), .q(q));
endmodule

// Two reset nets (the top reset and its synchronized copy) under one port
// name: two reset domains, u_a.q -> u_b.q crosses them.
module cf_rst_same_name (input logic clk, input logic rst_n, input logic d, output logic q);
    logic rst_meta, rst_sync, a_q;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) {rst_sync, rst_meta} <= 2'b00;
        else {rst_sync, rst_meta} <= {rst_meta, 1'b1};
    cfr_ff_b u_a (.clk(clk), .rst_n(rst_n), .d(d), .q(a_q));
    cfr_ff_b u_b (.clk(clk), .rst_n(rst_sync), .d(a_q), .q(q));
endmodule

// Two independent asynchronous resets, one clock: a_q -> q crosses them.
module cf_rst_two_ports (input logic clk, input logic rst_a_n, input logic rst_b_n,
                         input logic d, output logic q);
    logic a_q;
    always_ff @(posedge clk or negedge rst_a_n)
        if (!rst_a_n) a_q <= 1'b0;
        else a_q <= d;
    always_ff @(posedge clk or negedge rst_b_n)
        if (!rst_b_n) q <= 1'b0;
        else q <= a_q;
endmodule

// One synchronized reset for every data register: no crossing.
module cf_rst_sync_all (input logic clk, input logic rst_n, input logic d, output logic q);
    logic rst_meta, rst_sync, a_q;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) {rst_sync, rst_meta} <= 2'b00;
        else {rst_sync, rst_meta} <= {rst_meta, 1'b1};
    always_ff @(posedge clk or negedge rst_sync)
        if (!rst_sync) a_q <= 1'b0;
        else a_q <= d;
    always_ff @(posedge clk or negedge rst_sync)
        if (!rst_sync) q <= 1'b0;
        else q <= a_q;
endmodule

// A reset synchronized in clk_a used as the asynchronous reset of clk_b
// registers: its release is not synchronized to clk_b.
module cf_rst_wrong_domain (input logic clk_a, input logic clk_b, input logic rst_n,
                            input logic d, output logic q);
    logic rst_meta, rst_sync_a, b_r;
    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) {rst_sync_a, rst_meta} <= 2'b00;
        else {rst_sync_a, rst_meta} <= {rst_meta, 1'b1};
    always_ff @(posedge clk_b or negedge rst_sync_a)
        if (!rst_sync_a) b_r <= 1'b0;
        else b_r <= d;
    assign q = b_r;
endmodule

// Active-high and active-low use of the same reset net: one reset net, two
// polarities.
module cf_rst_polarity (input logic clk, input logic rst, input logic d, output logic q);
    logic a_q;
    always_ff @(posedge clk or posedge rst)
        if (rst) a_q <= 1'b0;
        else a_q <= d;
    always_ff @(posedge clk or negedge rst)
        if (!rst) q <= 1'b0;
        else q <= a_q;
endmodule
