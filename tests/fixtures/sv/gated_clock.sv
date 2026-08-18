module gated_clock (
    input  logic clk, en, rst_n,
    input  logic d,
    output logic q
);
    logic clk_en;
    assign clk_en = clk & en;

    logic ff_out;

    always_ff @(posedge clk_en or negedge rst_n)
        if (!rst_n) ff_out <= 1'b0; else ff_out <= d;

    assign q = ff_out;
endmodule
