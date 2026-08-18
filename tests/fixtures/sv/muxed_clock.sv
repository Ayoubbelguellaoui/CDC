module muxed_clock (
    input  logic clk_sel,
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);
    logic selected_clk;
    assign selected_clk = clk_sel ? clk_a : clk_b;

    logic sync_ff;
    always_ff @(posedge selected_clk or negedge rst_n) begin
        if (!rst_n)
            sync_ff <= 1'b0;
        else
            sync_ff <= data_in;
    end

    logic out_ff;
    always_ff @(posedge selected_clk or negedge rst_n) begin
        if (!rst_n)
            out_ff <= 1'b0;
        else
            out_ff <= sync_ff;
    end

    assign data_out = out_ff;
endmodule
