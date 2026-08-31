module gated_crossing_cdc004 (
    input  logic clk,
    input  logic clk_b,
    input  logic rst_n,
    input  logic gate,
    input  logic d,
    output logic q
);

    logic gated_clk;
    logic r1;
    logic r2;

    assign gated_clk = clk & gate;

    always_ff @(posedge gated_clk or negedge rst_n) begin
        if (!rst_n)
            r1 <= 1'b0;
        else
            r1 <= d;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            r2 <= 1'b0;
        else
            r2 <= r1;
    end

    assign q = r2;

endmodule
