module sync_misuse (
    input  logic       clk_a, clk_b, rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    logic [7:0] src_ff;
    logic [7:0] meta;
    logic [7:0] sync_reg;

    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) src_ff <= 8'd0; else src_ff <= data_in;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta    <= 8'd0;
            sync_reg <= 8'd0;
        end else begin
            meta    <= src_ff;
            sync_reg <= meta;
        end
    end

    assign data_out = sync_reg;
endmodule
