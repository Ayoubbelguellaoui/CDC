module missing_reset_adversarial (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);

    // Both registers have reset — should NOT trigger CDC007
    logic src_with_reset;
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_with_reset <= 1'b0;
        else
            src_with_reset <= data_in;
    end

    logic dst_with_reset;
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            dst_with_reset <= 1'b0;
        else
            dst_with_reset <= src_with_reset;
    end

    // Neither register has reset — SHOULD trigger CDC007
    logic src_no_reset;
    always_ff @(posedge clk_a) begin
        src_no_reset <= data_in;
    end

    logic dst_no_reset;
    always_ff @(posedge clk_b) begin
        dst_no_reset <= src_no_reset;
    end

    // Only source has reset — should NOT trigger CDC007
    logic src_only_reset;
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_only_reset <= 1'b0;
        else
            src_only_reset <= data_in;
    end

    logic dst_no_reset2;
    always_ff @(posedge clk_b) begin
        dst_no_reset2 <= src_only_reset;
    end

    assign data_out = dst_with_reset | dst_no_reset | dst_no_reset2;

endmodule
