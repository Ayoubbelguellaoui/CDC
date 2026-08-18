// sync_2ff.sv — Example: Safe 2FF synchronizer (no error expected)
//
// Run: opencdc check examples/sync_2ff.sv --top sync_2ff_example
// Expected: 0 findings

module sync_2ff_example (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);
    logic src_ff;
    logic meta;
    logic sync_ff;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= 1'b0;
        else
            src_ff <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            meta <= 1'b0;
        else
            meta <= src_ff;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            sync_ff <= 1'b0;
        else
            sync_ff <= meta;
    end

    assign data_out = sync_ff;
endmodule
