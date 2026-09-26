`timescale 1ns/1ps

// Known violation: FIFO-style pointer exchange done WRONG — the binary
// (non-gray) write/read pointers cross domains directly with no gray
// encoding and no synchronizer. Several bits can change at once, so the
// receiving side routinely samples a transient, never-written value.
// Expected: CDC001 + CDC002 on both pointer crossings.

module async_fifo_binary_broken (
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

    reg [7:0]     mem [0:15];

    reg [PTR_W:0] waddr_bin;
    reg [PTR_W:0] raddr_bin;
    // BUG: binary pointers sampled straight across the boundary.
    reg [PTR_W:0] waddr_bin_seen_in_rclk;
    reg [PTR_W:0] raddr_bin_seen_in_wclk;

    always @(posedge wr_clk or negedge rst_n) begin
        if (!rst_n) begin
            waddr_bin <= 0;
        end else if (wr_en && !full) begin
            mem[waddr_bin[PTR_W-1:0]] <= wr_data;
            waddr_bin <= waddr_bin + 1'b1;
        end
    end

    // BUG: no gray code, no sync stages.
    always @(posedge wr_clk or negedge rst_n) begin
        if (!rst_n) raddr_bin_seen_in_wclk <= 0;
        else raddr_bin_seen_in_wclk <= raddr_bin;
    end

    always @(posedge rd_clk or negedge rst_n) begin
        if (!rst_n) begin
            raddr_bin <= 0;
        end else if (rd_en && !empty) begin
            raddr_bin <= raddr_bin + 1'b1;
        end
    end

    // BUG: no gray code, no sync stages.
    always @(posedge rd_clk or negedge rst_n) begin
        if (!rst_n) waddr_bin_seen_in_rclk <= 0;
        else waddr_bin_seen_in_rclk <= waddr_bin;
    end

    assign rd_data = mem[raddr_bin[PTR_W-1:0]];
    assign full  = ((waddr_bin - raddr_bin_seen_in_wclk) == 16);
    assign empty = (raddr_bin == waddr_bin_seen_in_rclk);

endmodule
