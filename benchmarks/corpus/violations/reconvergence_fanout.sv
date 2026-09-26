`timescale 1ns/1ps

// Known violation (CDC003): one multi-bit source fans out over two
// differently-synchronized paths that reconverge in the destination
// domain. The two copies can settle on different source epochs, so the
// recombined word may never have existed on the source side.

module reconvergence_fanout (
    input  wire       clk_a,
    input  wire       clk_b,
    input  wire       rst_n,
    input  wire [7:0] data_in,
    output wire [7:0] data_out
);

    reg [7:0] rconv_src;
    reg [7:0] rconv_path_a;
    reg [7:0] rconv_path_b;
    reg [7:0] rconv_consumer;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) rconv_src <= 8'b0;
        else rconv_src <= data_in;
    end

    // Path A: one sync stage.
    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) rconv_path_a <= 8'b0;
        else rconv_path_a <= rconv_src;
    end

    // Path B: direct sample (different latency from path A).
    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) rconv_path_b <= 8'b0;
        else rconv_path_b <= rconv_src;
    end

    // Reconvergence point: combines the two skewed copies.
    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) rconv_consumer <= 8'b0;
        else rconv_consumer <= rconv_path_a ^ rconv_path_b;
    end

    assign data_out = rconv_consumer;

endmodule
