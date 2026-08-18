module sync_3ff (
    input  logic clk_b, rst_n,
    input  logic async_in,
    output logic sync_out
);
    logic meta1, meta2, sync;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta1 <= 1'b0;
            meta2 <= 1'b0;
            sync  <= 1'b0;
        end else begin
            meta1 <= async_in;
            meta2 <= meta1;
            sync  <= meta2;
        end
    end

    assign sync_out = sync;
endmodule
