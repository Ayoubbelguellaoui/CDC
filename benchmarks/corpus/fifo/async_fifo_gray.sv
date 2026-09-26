`timescale 1ns/1ps

// Correct design: real asynchronous FIFO, depth 16, 8-bit data.
// Write and read pointers are binary counters converted to gray code;
// only the gray pointers cross domains, each through a 2FF synchronizer.
// Full/empty are derived from synchronized gray pointers.
// [CORPUS NOTE — verified 2026-09-26] This is a CORRECT async FIFO, yet
// the gray pointer crossings wr_ptr_gray -> wr_ptr_gray_sync1 and
// rd_ptr_gray -> rd_ptr_gray_sync1 are reported as CDC001 error (no
// CDC002 — gray IS recognized for the multi-bit rule). Root cause: the
// synchronizer matcher only classifies single-bit (width == 1) chains
// (SynchronizerMatcher::find_pattern_for_dest + chain extension), so the
// 5-bit-wide pointer sync chains are never verified_safe. A documented
// false-positive class for wide gray-coded sync chains. CDC013 warnings
// on the synced pointers are also emitted (unknown-propagation). Compare
// with async_fifo_binary_broken.sv, where CDC002 correctly fires.

module async_fifo_gray (
    input  wire       wr_clk,
    input  wire       rd_clk,
    input  wire       rst_n,
    input  wire       wr_en,
    input  wire [7:0] wr_data,
    input  wire       rd_en,
    output wire [7:0] rd_data,
    output wire       full,
    output wire       empty
);

    localparam int unsigned PTR_W = 4;

    function [PTR_W:0] bin2gray;
        input [PTR_W:0] b;
        begin
            bin2gray = b ^ (b >> 1);
        end
    endfunction

    reg [7:0]     mem [0:15];

    // Write domain.
    reg [PTR_W:0] wr_ptr_bin;
    reg [PTR_W:0] wr_ptr_gray;
    reg [PTR_W:0] rd_ptr_gray_sync1;
    reg [PTR_W:0] rd_ptr_gray_sync2;

    // Read domain.
    reg [PTR_W:0] rd_ptr_bin;
    reg [PTR_W:0] rd_ptr_gray;
    reg [PTR_W:0] wr_ptr_gray_sync1;
    reg [PTR_W:0] wr_ptr_gray_sync2;

    always @(posedge wr_clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr_bin <= 0;
            wr_ptr_gray <= 0;
        end else begin
            if (wr_en && !full) begin
                mem[wr_ptr_bin[PTR_W-1:0]] <= wr_data;
                wr_ptr_bin <= wr_ptr_bin + 1'b1;
            end
            wr_ptr_gray <= bin2gray(wr_ptr_bin);
        end
    end

    // Read pointer (gray) synchronized into the write domain.
    always @(posedge wr_clk or negedge rst_n) begin
        if (!rst_n) begin
            rd_ptr_gray_sync1 <= 0;
            rd_ptr_gray_sync2 <= 0;
        end else begin
            rd_ptr_gray_sync1 <= rd_ptr_gray;
            rd_ptr_gray_sync2 <= rd_ptr_gray_sync1;
        end
    end

    always @(posedge rd_clk or negedge rst_n) begin
        if (!rst_n) begin
            rd_ptr_bin <= 0;
            rd_ptr_gray <= 0;
        end else begin
            if (rd_en && !empty)
                rd_ptr_bin <= rd_ptr_bin + 1'b1;
            rd_ptr_gray <= bin2gray(rd_ptr_bin);
        end
    end

    // Write pointer (gray) synchronized into the read domain.
    always @(posedge rd_clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr_gray_sync1 <= 0;
            wr_ptr_gray_sync2 <= 0;
        end else begin
            wr_ptr_gray_sync1 <= wr_ptr_gray;
            wr_ptr_gray_sync2 <= wr_ptr_gray_sync1;
        end
    end

    assign rd_data = mem[rd_ptr_bin[PTR_W-1:0]];
    assign full  = (wr_ptr_gray == {~rd_ptr_gray_sync2[PTR_W:PTR_W-1],
                                    rd_ptr_gray_sync2[PTR_W-2:0]});
    assign empty = (rd_ptr_gray == wr_ptr_gray_sync2);

endmodule
