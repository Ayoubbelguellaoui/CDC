module muxed_crossing (
    input  logic clk_sel,
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);
    logic selected_clk;
    assign selected_clk = clk_sel ? clk_a : clk_b;

    logic src_ff;
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= 1'b0;
        else
            src_ff <= data_in;
    end

    logic dst_ff;
    always_ff @(posedge selected_clk or negedge rst_n) begin
        if (!rst_n)
            dst_ff <= 1'b0;
        else
            dst_ff <= src_ff;
    end

    assign data_out = dst_ff;
endmodule
