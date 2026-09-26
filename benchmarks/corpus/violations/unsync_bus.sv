`timescale 1ns/1ps

// Known violations: multi-bit crossings with no safe transfer scheme.
//   1. Plain 8-bit data bus sampled directly (CDC001 + CDC002).
//   2. Free-running BINARY counter sampled directly (CDC001 + CDC002).
//      A binary counter may change several bits per tick, so direct
//      sampling can capture a completely bogus value.

module unsync_bus (
    input  wire       clk_a,
    input  wire       clk_b,
    input  wire       rst_n,
    input  wire [7:0] data_in,
    output wire [7:0] bus_out,
    output wire [3:0] cnt_out
);

    reg [7:0] bus_src;
    reg [7:0] bus_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) bus_src <= 8'b0;
        else bus_src <= data_in;
    end

    // BUG: multi-bit bus crosses with no gray code, no handshake, no FIFO.
    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) bus_dst <= 8'b0;
        else bus_dst <= bus_src;
    end

    reg [3:0] bincnt_src;
    reg [3:0] bincnt_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) bincnt_src <= 4'b0;
        else bincnt_src <= bincnt_src + 1'b1;
    end

    // BUG: binary (non-gray) counter crosses with no synchronization.
    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) bincnt_dst <= 4'b0;
        else bincnt_dst <= bincnt_src;
    end

    assign bus_out = bus_dst;
    assign cnt_out = bincnt_dst;

endmodule
