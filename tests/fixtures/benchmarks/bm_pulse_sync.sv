module bm_pulse_sync (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic        pulse_in,
    output logic        pulse_out
);
    logic toggle_src;
    always_ff @(posedge clk_a) begin
        if (pulse_in) toggle_src <= ~toggle_src;
    end
    logic sync_ff1, sync_ff2;
    always_ff @(posedge clk_b) begin
        sync_ff1 <= toggle_src;
        sync_ff2 <= sync_ff1;
    end
    assign pulse_out = sync_ff1 ^ sync_ff2;
endmodule
