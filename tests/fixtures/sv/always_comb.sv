module always_comb_model (
    input  logic clk,
    input  logic d,
    output logic out
);
    logic next_q;

    always_comb begin
        next_q = d;
    end

    logic q;
    always_ff @(posedge clk) begin
        q <= next_q;
    end

    assign out = q;
endmodule
