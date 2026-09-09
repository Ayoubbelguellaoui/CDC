module bm_reset_sync (
    input  logic        clk,
    input  logic        rst_n,
    output logic        rst_sync_n
);
    logic rst_ff1;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rst_ff1 <= 1'b0;
            rst_sync_n <= 1'b0;
        end else begin
            rst_ff1 <= 1'b1;
            rst_sync_n <= rst_ff1;
        end
    end
endmodule
