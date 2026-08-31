module complex_lv_crossing (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic        rst_n,
    input  logic [15:0] data_in,
    output logic [15:0] data_out
);
    logic [15:0] src_ff;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= '0;
        else
            src_ff <= data_in;
    end

    logic [7:0] meta_lo;
    logic [7:0] meta_hi;
    logic [7:0] sync_lo;
    logic [7:0] sync_hi;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta_lo <= '0;
            meta_hi <= '0;
            sync_lo <= '0;
            sync_hi <= '0;
        end else begin
            meta_lo <= src_ff[7:0];
            meta_hi <= src_ff[15:8];
            sync_lo <= meta_lo;
            sync_hi <= meta_hi;
        end
    end

    assign data_out = {sync_hi, sync_lo};
endmodule
