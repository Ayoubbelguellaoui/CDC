module bm_gated_clock (
    input  logic        clk,
    input  logic        clk_en,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out
);
    logic gated_clk;
    assign gated_clk = clk & clk_en;
    logic [7:0] data_reg;
    always_ff @(posedge gated_clk or negedge rst_n) begin
        if (!rst_n) data_reg <= 8'b0;
        else data_reg <= data_in;
    end
    assign data_out = data_reg;
endmodule
