module reconvergence_safe (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);
    logic src_ff;
    logic meta1;
    logic sync1;
    logic meta2;
    logic sync2;
    logic consumer_ff;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= 1'b0;
        else
            src_ff <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            meta1 <= 1'b0;
        else
            meta1 <= src_ff;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            sync1 <= 1'b0;
        else
            sync1 <= meta1;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            meta2 <= 1'b0;
        else
            meta2 <= src_ff;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            sync2 <= 1'b0;
        else
            sync2 <= meta2;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            consumer_ff <= 1'b0;
        else
            consumer_ff <= sync1 | sync2;
    end

    assign data_out = consumer_ff;
endmodule
