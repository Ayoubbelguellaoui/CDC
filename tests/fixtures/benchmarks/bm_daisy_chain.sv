module bm_daisy_chain (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic        clk_c,
    input  logic        data_in,
    output logic        data_out
);
    logic sync1_ff1, sync1_ff2;
    always_ff @(posedge clk_b) begin
        sync1_ff1 <= data_in;
        sync1_ff2 <= sync1_ff1;
    end
    logic sync2_ff1, sync2_ff2;
    always_ff @(posedge clk_c) begin
        sync2_ff1 <= sync1_ff2;
        sync2_ff2 <= sync2_ff1;
    end
    assign data_out = sync2_ff2;
endmodule
