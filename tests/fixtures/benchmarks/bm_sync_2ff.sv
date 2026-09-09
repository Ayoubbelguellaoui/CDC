module bm_sync_2ff (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic        sig_in,
    output logic        sig_out
);
    logic sync_ff1, sync_ff2;
    always_ff @(posedge clk_b) begin
        sync_ff1 <= sig_in;
        sync_ff2 <= sync_ff1;
    end
    assign sig_out = sync_ff2;
endmodule
