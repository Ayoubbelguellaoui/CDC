module generate_crossing (
    input  logic        clk_a,
    input  logic        clk_b,
    input  logic        rst_n,
    input  logic [3:0]  sel,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out
);
    logic [7:0] src_ff;
    logic [7:0] meta_ff;
    logic [7:0] sync_ff;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= '0;
        else
            src_ff <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta_ff <= '0;
            sync_ff <= '0;
        end else begin
            meta_ff <= src_ff;
            sync_ff <= meta_ff;
        end
    end

    assign data_out = sync_ff;

    for (genvar i = 0; i < 4; i++) begin : gen_channels
        logic [7:0] local_reg;

        always_ff @(posedge clk_b or negedge rst_n) begin
            if (!rst_n)
                local_reg <= '0;
            else if (sel[i])
                local_reg <= sync_ff;
        end
    end
endmodule
