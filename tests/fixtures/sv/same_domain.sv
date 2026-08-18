module simple_same_domain (
    input  logic clk_a,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);

    logic ff_a;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            ff_a <= 1'b0;
        else
            ff_a <= data_in;
    end

    assign data_out = ff_a;

endmodule
