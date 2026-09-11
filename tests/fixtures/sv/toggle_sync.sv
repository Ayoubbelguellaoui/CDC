module toggle_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic pulse_in,
    output logic pulse_out
);
    logic toggle;
    logic sync1;
    logic sync2;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            toggle <= 1'b0;
        else if (pulse_in)
            toggle <= ~toggle;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync1 <= 1'b0;
            sync2 <= 1'b0;
        end else begin
            sync1 <= toggle;
            sync2 <= sync1;
        end
    end

    assign pulse_out = sync2;

endmodule
