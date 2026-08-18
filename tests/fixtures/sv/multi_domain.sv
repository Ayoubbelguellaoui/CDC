module multi_domain (
    input  logic clk_a,
    input  logic clk_b,
    input  logic clk_c,
    input  logic rst_n,
    input  logic d,
    output logic q
);

    logic r1;
    logic r2;
    logic r3;

    always_ff @(posedge clk_a or negedge rst_n) begin
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

    always_ff @(posedge clk_c or negedge rst_n) begin
        if (!rst_n)
            r3 <= 1'b0;
        else
            r3 <= r2;
    end

    assign q = r3;

endmodule
