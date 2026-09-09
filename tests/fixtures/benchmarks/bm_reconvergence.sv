module bm_reconvergence (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        sel,
    input  logic [7:0]  a,
    input  logic [7:0]  b,
    output logic [7:0]  y
);
    logic [7:0] mux_out;
    logic [7:0] path1, path2;
    assign mux_out = sel ? a : b;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) path1 <= 8'b0;
        else path1 <= mux_out;
    end
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) path2 <= 8'b0;
        else path2 <= mux_out;
    end
    assign y = path1 + path2;
endmodule
