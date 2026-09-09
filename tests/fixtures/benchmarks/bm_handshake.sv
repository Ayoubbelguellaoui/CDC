module bm_handshake (
    input  logic        clk_src,
    input  logic        clk_dst,
    input  logic [31:0] data_in,
    input  logic        valid_in,
    output logic        ready_out,
    output logic [31:0] data_out,
    output logic        valid_out,
    input  logic        ready_in
);
    logic [31:0] data_reg;
    logic        valid_reg;
    always_ff @(posedge clk_src) begin
        if (valid_in && ready_out) begin
            data_reg <= data_in;
            valid_reg <= 1'b1;
        end else if (ready_in) begin
            valid_reg <= 1'b0;
        end
    end
    assign data_out = data_reg;
    assign valid_out = valid_reg;
    assign ready_out = ~valid_reg || ready_in;
endmodule
