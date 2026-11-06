// Clocks derived inside the design. dc_div: a toggling register divides
// clk_a - the same root, no crossing. dc_mux: `sel ? clk_b : clk_a` - a
// clock domain of its own, so a register on it fed from clk_a crosses.
module dc_div (input logic clk_a, input logic d, output logic q);
    logic div, a_r;
    always_ff @(posedge clk_a) div <= ~div;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge div) q <= a_r;
endmodule

module dc_mux (input logic clk_a, input logic clk_b, input logic sel, input logic d,
               output logic q);
    logic mclk, a_r;
    assign mclk = sel ? clk_b : clk_a;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge mclk) q <= a_r;
endmodule
