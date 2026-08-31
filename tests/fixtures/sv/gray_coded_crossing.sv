module gray_coded_crossing (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);

    logic [7:0] gray_src_reg;
    logic [7:0] gray_dst_reg;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            gray_src_reg <= 8'b0;
        else
            gray_src_reg <= data_in ^ (data_in >> 1);
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            gray_dst_reg <= 8'b0;
        else
            gray_dst_reg <= gray_src_reg;
    end

    assign data_out = gray_dst_reg ^ (gray_dst_reg >> 1);

endmodule
