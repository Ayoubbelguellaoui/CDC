module gated_clock_struct (
    input  logic clk,
    input  logic en,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    logic clk_gate;
    logic ff_out;

    assign clk_gate = clk & en;

    always_ff @(posedge clk_gate or negedge rst_n)
        if (!rst_n) ff_out <= 1'b0; else ff_out <= d;

    assign q = ff_out;
endmodule
