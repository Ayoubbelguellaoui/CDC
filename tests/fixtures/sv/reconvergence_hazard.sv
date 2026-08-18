module reconvergence_hazard (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);
    logic src_ff;
    logic dst1_ff;
    logic dst2_ff;
    logic consumer_ff;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= 1'b0;
        else
            src_ff <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            dst1_ff <= 1'b0;
        else
            dst1_ff <= src_ff;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            dst2_ff <= 1'b0;
        else
            dst2_ff <= src_ff;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            consumer_ff <= 1'b0;
        else
            consumer_ff <= dst1_ff | dst2_ff;
    end

    assign data_out = consumer_ff;
endmodule
