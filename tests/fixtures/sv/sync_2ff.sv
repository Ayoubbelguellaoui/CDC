module sync_2ff (
    input  logic clk_b,
    input  logic rst_n,
    input  logic async_in,
    output logic sync_out
);

    logic meta;
    logic sync;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta  <= 1'b0;
            sync  <= 1'b0;
        end else begin
            meta  <= async_in;
            sync  <= meta;
        end
    end

    assign sync_out = sync;

endmodule
