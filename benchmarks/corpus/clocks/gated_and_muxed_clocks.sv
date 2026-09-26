`timescale 1ns/1ps

// Clock gating + clock mux coverage in one file.
//   1. CDC004 positive: source register clocked by an AND-gated clock
//      (clk_raw & gate_en) crosses into clk_b with no synchronizer.
//   2. CDC005 positive: source register clocked by a muxed clock
//      (sel ? clk_x : clk_y) WITHOUT reset crosses into clk_b.
//   3. CDC005 negative: same muxed clock, but the register HAS reset,
//      so CDC005 must not fire (CDC001 still applies to the crossing).

module gated_and_muxed_clocks (
    input  wire clk_raw,
    input  wire clk_x,
    input  wire clk_y,
    input  wire clk_b,
    input  wire rst_n,
    input  wire gate_en,
    input  wire mux_sel,
    input  wire data_in,
    output wire gated_out,
    output wire muxed_out,
    output wire muxed_rst_out
);

    wire gated_clk;
    assign gated_clk = clk_raw & gate_en;
    wire muxed_clk;
    assign muxed_clk = mux_sel ? clk_x : clk_y;

    reg gated_src;
    reg gated_dst;

    // CDC004: gated-clock source crosses domains, no sync.
    always @(posedge gated_clk or negedge rst_n) begin
        if (!rst_n) gated_src <= 1'b0;
        else gated_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) gated_dst <= 1'b0;
        else gated_dst <= gated_src;
    end

    reg muxed_src_norst;
    reg muxed_dst_a;

    // CDC005: muxed clock, no reset, crosses domains, no sync.
    always @(posedge muxed_clk) begin
        muxed_src_norst <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) muxed_dst_a <= 1'b0;
        else muxed_dst_a <= muxed_src_norst;
    end

    reg muxed_src_rst;
    reg muxed_dst_b;

    // Negative control: muxed clock WITH reset — CDC005 must stay quiet.
    always @(posedge muxed_clk or negedge rst_n) begin
        if (!rst_n) muxed_src_rst <= 1'b0;
        else muxed_src_rst <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) muxed_dst_b <= 1'b0;
        else muxed_dst_b <= muxed_src_rst;
    end

    assign gated_out     = gated_dst;
    assign muxed_out     = muxed_dst_a;
    assign muxed_rst_out = muxed_dst_b;

endmodule
