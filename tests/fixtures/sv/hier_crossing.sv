module child_sync (
    input  logic clk_b,
    input  logic rst_n,
    input  logic d_in,
    output logic d_out
);
    logic meta_ff;
    logic sync_ff;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta_ff <= 1'b0;
            sync_ff <= 1'b0;
        end else begin
            meta_ff <= d_in;
            sync_ff <= meta_ff;
        end
    end

    assign d_out = sync_ff;
endmodule

module hier_crossing (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    logic src_ff;
    logic sync_out;

    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) src_ff <= 1'b0; else src_ff <= d;

    child_sync u_sync (
        .clk_b  (clk_b),
        .rst_n  (rst_n),
        .d_in   (src_ff),
        .d_out  (sync_out)
    );

    assign q = sync_out;
endmodule
