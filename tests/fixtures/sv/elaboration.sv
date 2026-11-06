// Elaborating real designs. el_top: the top of the file; el_unused is a
// type-parameterized module nothing instantiates - elaborated on its own with
// its default type (logic) the member access is an error, so it must not be
// taken for a top. el_top uses a signal above its declaration (accepted by
// the tools designs are built with), has a concurrent assertion (no logic),
// and an `always @(*)` block on the path of a crossing.
typedef struct packed {
    logic field;
} el_s_t;

module el_unused #(parameter type T = logic) (input T x, output logic y);
    assign y = x.field;
endmodule

module el_top (input logic clk_a, input logic clk_b, input logic d, input logic e,
               output logic q);
    logic a_r;
    always_ff @(posedge clk_a) a_r <= d;
    assign early = a_r;
    logic early;
    logic w;
    always @(*) w = early & e;
    always_ff @(posedge clk_b) q <= w;
    assert property (@(posedge clk_b) q |-> w);
endmodule
