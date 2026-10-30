`timescale 1ns/1ps

// CDC conformance: design structure - hierarchy, generate, arrays, loops,
// clock sources.

module cfs_ff (input logic clk, input logic d, output logic q);
    always_ff @(posedge clk) q <= d;
endmodule

module cfs_sync2 (input logic clk, input logic d, output logic q);
    logic s0, s1;
    always_ff @(posedge clk) begin
        s0 <= d;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// A child register drives a parent register of the other clock.
module cf_child_to_parent (input logic clk_a, input logic clk_b, input logic d,
                           output logic q);
    logic a_q;
    cfs_ff u_a (.clk(clk_a), .d(d), .q(a_q));
    always_ff @(posedge clk_b) q <= a_q;
endmodule

// A child register drives a sibling of the other clock.
module cf_sibling (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_q;
    cfs_ff u_a (.clk(clk_a), .d(d), .q(a_q));
    cfs_ff u_b (.clk(clk_b), .d(a_q), .q(q));
endmodule

// A parent register synchronized by a child synchronizer module: correct.
module cf_parent_to_sync (input logic clk_a, input logic clk_b, input logic d,
                          output logic q);
    logic a_r;
    always_ff @(posedge clk_a) a_r <= d;
    cfs_sync2 u_sync (.clk(clk_b), .d(a_r), .q(q));
endmodule

// The clock reaches the child under another port name, through two levels.
module cfs_wrap (input logic clk_in, input logic d, output logic q);
    cfs_sync2 u_inner (.clk(clk_in), .d(d), .q(q));
endmodule

module cf_deep_sync (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r;
    always_ff @(posedge clk_a) a_r <= d;
    cfs_wrap u_wrap (.clk_in(clk_b), .d(a_r), .q(q));
endmodule

// A generate loop of unsynchronized crossings, one per iteration.
module cf_generate_unsync (input logic clk_a, input logic clk_b, input logic [3:0] d,
                           output logic [3:0] q);
    for (genvar i = 0; i < 4; i++) begin : g
        logic a_r;
        always_ff @(posedge clk_a) a_r <= d[i];
        always_ff @(posedge clk_b) q[i] <= a_r;
    end
endmodule

// A generate loop of correct synchronizer instances.
module cf_generate_sync (input logic clk_a, input logic clk_b, input logic [3:0] d,
                         output logic [3:0] q);
    for (genvar i = 0; i < 4; i++) begin : g
        logic a_r;
        always_ff @(posedge clk_a) a_r <= d[i];
        cfs_sync2 u_sync (.clk(clk_b), .d(a_r), .q(q[i]));
    end
endmodule

// A synchronizer whose stages are an unpacked array shifted by a for loop.
module cf_array_loop_sync (input logic clk_a, input logic clk_b, input logic d,
                           output logic q);
    localparam int N = 3;
    logic a_r;
    logic s[N];
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s[0] <= a_r;
        for (int i = 1; i < N; i++) s[i] <= s[i-1];
    end
    assign q = s[N-1];
endmodule

// Clocks from a black-box PLL (undriven outputs) through a buffer instance.
module cfs_pll (input logic clk_in, output logic clk0, output logic clk1);
endmodule

module cfs_buf (input logic i, output logic o);
    assign o = i;
endmodule

module cf_pll_clocks (input logic clk_ref, input logic d, output logic q_sync,
                      output logic q_raw);
    logic c0_raw, c1_raw, c0, c1, a_r;
    cfs_pll u_pll (.clk_in(clk_ref), .clk0(c0_raw), .clk1(c1_raw));
    cfs_buf u_b0 (.i(c0_raw), .o(c0));
    cfs_buf u_b1 (.i(c1_raw), .o(c1));
    always_ff @(posedge c0) a_r <= d;
    cfs_sync2 u_sync (.clk(c1), .d(a_r), .q(q_sync));
    always_ff @(posedge c1) q_raw <= a_r;
endmodule

// A clock-gate instance: its output is the input clock gated, one domain.
module cfs_clock_gate (input logic clk_i, input logic en_i, output logic clk_o);
    logic en_latch;
    always_latch begin
        if (!clk_i) en_latch = en_i;
    end
    assign clk_o = en_latch & clk_i;
endmodule

module cf_clock_gate (input logic clk_a, input logic en, input logic d, output logic q);
    logic gclk, a_r;
    cfs_clock_gate u_cg (.clk_i(clk_a), .en_i(en), .clk_o(gclk));
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge gclk) q <= a_r;
endmodule

// A divided clock (a toggling register): folded into its parent root.
module cf_clock_div (input logic clk_a, input logic d, output logic q);
    logic div, a_r;
    always_ff @(posedge clk_a) div <= ~div;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge div) q <= a_r;
endmodule

// A clock mux selecting clk_a or clk_b: registers on it see both clocks.
module cf_clock_mux (input logic clk_a, input logic clk_b, input logic sel, input logic d,
                     output logic q);
    logic mclk, a_r;
    assign mclk = sel ? clk_b : clk_a;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge mclk) q <= a_r;
endmodule
