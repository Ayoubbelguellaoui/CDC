// waived_finding.sv — Example: CDC crossing suppressed by waiver
//
// Run: opencdc check examples/waived_finding.sv --top waived_example --waiver examples/waivers.txt
// Expected: 1 finding (waived), exit 0

module waived_example (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);
    logic src_ff;
    logic dst_ff;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= 1'b0;
        else
            src_ff <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            dst_ff <= 1'b0;
        else
            dst_ff <= src_ff;
    end

    assign data_out = dst_ff;
endmodule
