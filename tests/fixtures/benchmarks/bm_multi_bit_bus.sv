module bm_multi_bit_bus (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic [63:0] bus_in,
    output logic [63:0] bus_out
);
    logic [63:0] bus_reg;
    always_ff @(posedge clk_a) bus_reg <= bus_in;
    assign bus_out = bus_reg;
endmodule
