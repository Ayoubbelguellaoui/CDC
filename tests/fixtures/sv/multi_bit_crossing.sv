module multi_bit_crossing (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);

    logic [7:0] src_reg;
    logic [7:0] dst_reg;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_reg <= 8'b0;
        else
            src_reg <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            dst_reg <= 8'b0;
        else
            dst_reg <= src_reg;
    end

    assign data_out = dst_reg;

endmodule
