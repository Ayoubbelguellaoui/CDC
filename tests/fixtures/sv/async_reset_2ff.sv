module async_reset_2ff (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);

    logic meta;

    // Classic async-reset form: reset first in the event list. The clock is
    // 'clk' (last non-reset event) and the async reset is 'rst_n'.
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            meta <= 1'b0;
        else
            meta <= d;
    end

    always_ff @(posedge clk) begin
        q <= meta;
    end

endmodule
