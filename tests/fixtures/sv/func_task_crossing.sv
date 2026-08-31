module func_task_crossing (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);

    function automatic logic [7:0] process_data(
        input logic [7:0] din1,
        input logic [7:0] din2
    );
        return din1 + din2;
    endfunction

    logic [7:0] src_reg;
    logic [7:0] dst_reg;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_reg <= 8'b0;
        else
            src_reg <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            dst_reg <= 8'b0;
        else
            dst_reg <= process_data(src_reg, 8'h42);
    end

    assign data_out = dst_reg;

endmodule

module task_crossing (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);

    task automatic compute(
        input  logic [7:0] a,
        input  logic [7:0] b,
        output logic [7:0] result
    );
        result = a ^ b;
    endtask

    logic [7:0] src_reg;
    logic [7:0] dst_reg;
    logic [7:0] tmp;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_reg <= 8'b0;
        else
            src_reg <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            dst_reg <= 8'b0;
            tmp     <= 8'b0;
        end else begin
            compute(src_reg, 8'hFF, tmp);
            dst_reg <= tmp;
        end
    end

    assign data_out = dst_reg;

endmodule
