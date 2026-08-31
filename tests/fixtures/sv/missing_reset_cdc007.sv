module missing_reset_cdc007 (
    input  logic clk_a,
    input  logic clk_b,
    input  logic d,
    output logic q
);

    logic r1;
    logic r2;

    always_ff @(posedge clk_a) begin
        r1 <= d;
    end

    always_ff @(posedge clk_b) begin
        r2 <= r1;
    end

    assign q = r2;

endmodule
