// Two instances of the same module connected to different clocks.
// Same port name "clk" in both instances, but connected to different
// top-level clocks. Should produce 2 separate clock domains.
module shared_sub (
    input  logic clk,
    input  logic d,
    output logic q
);
    logic ff;
    always_ff @(posedge clk)
        ff <= d;
    assign q = ff;
endmodule

module multi_instance_clock (
    input  logic clk_a,
    input  logic clk_b,
    input  logic d_a,
    input  logic d_b,
    output logic q_a,
    output logic q_b
);
    shared_sub inst_a (
        .clk (clk_a),
        .d   (d_a),
        .q   (q_a)
    );

    shared_sub inst_b (
        .clk (clk_b),
        .d   (d_b),
        .q   (q_b)
    );
endmodule
