`timescale 1ns/1ps

// CDC002 Benchmark: Multi-bit Crossing
// Positive: multi-bit bus without gray-code or handshake (MUST trigger CDC002)
// Negative: gray-coded bus (MUST NOT trigger CDC002)
// Negative: bus with handshake (MUST NOT trigger CDC002)
// Ambiguous: bus named "gray" but not actually gray-coded (documented)

module cdc002_benchmark (
    input  wire        clk_a,
    input  wire        clk_b,
    input  wire        rst_n,
    input  wire [7:0]  data_bus,
    input  wire [3:0]  counter_in
);

    // ====================================================================
    // POSITIVE CONTROL 1: Plain multi-bit bus crossing (MUST trigger CDC002)
    // ====================================================================
    reg [7:0] positive_bus_src;
    reg [7:0] positive_bus_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) positive_bus_src <= 8'b0;
        else positive_bus_src <= data_bus;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) positive_bus_dst <= 8'b0;
        else positive_bus_dst <= positive_bus_src;
    end

    // ====================================================================
    // POSITIVE CONTROL 2: Multi-bit counter crossing (MUST trigger CDC002)
    // ====================================================================
    reg [3:0] positive_cnt_src;
    reg [3:0] positive_cnt_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) positive_cnt_src <= 4'b0;
        else positive_cnt_src <= counter_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) positive_cnt_dst <= 4'b0;
        else positive_cnt_dst <= positive_cnt_src;
    end

    // ====================================================================
    // NEGATIVE CONTROL 1: True gray-coded crossing (MUST NOT trigger CDC002)
    // The signal name contains "gray" AND the encoding is structurally gray
    // ====================================================================
    reg [3:0] bin_counter;
    reg [3:0] gray_counter;
    reg [3:0] gray_dst_ff1;
    reg [3:0] gray_dst_ff2;

    function [3:0] bin2gray;
        input [3:0] b;
        begin
            bin2gray = b ^ (b >> 1);
        end
    endfunction

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) bin_counter <= 4'b0;
        else bin_counter <= bin_counter + 1;
    end

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) gray_counter <= 4'b0;
        else gray_counter <= bin2gray(bin_counter);
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            gray_dst_ff1 <= 4'b0;
            gray_dst_ff2 <= 4'b0;
        end else begin
            gray_dst_ff1 <= gray_counter;
            gray_dst_ff2 <= gray_dst_ff1;
        end
    end

    // ====================================================================
    // NEGATIVE CONTROL 2: Handshake-protected bus (MUST NOT trigger CDC002)
    // ====================================================================
    reg [7:0] hs_data_src;
    reg [7:0] hs_data_dst;
    reg       hs_req_src;
    reg       hs_ack_dst;
    reg       hs_req_sync1, hs_req_sync2;
    reg       hs_ack_sync1, hs_ack_sync2;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) begin
            hs_data_src <= 8'b0;
            hs_req_src <= 1'b0;
        end else begin
            hs_data_src <= data_bus;
            hs_req_src <= ~hs_req_src;
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            hs_req_sync1 <= 1'b0;
            hs_req_sync2 <= 1'b0;
        end else begin
            hs_req_sync1 <= hs_req_src;
            hs_req_sync2 <= hs_req_sync1;
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            hs_data_dst <= 8'b0;
            hs_ack_dst <= 1'b0;
        end else if (hs_req_sync2 != hs_ack_dst) begin
            hs_data_dst <= hs_data_src;
            hs_ack_dst <= hs_req_sync2;
        end
    end

    // ====================================================================
    // AMBIGUOUS 1: Signal named "gray" but NOT actually gray-coded
    // Expected: CDC002 fires (name-only detection would suppress, structural wouldn't)
    // Documented: This tests whether detection is name-based or structural
    // ====================================================================
    reg [7:0] gray_named_but_not_gray_src;
    reg [7:0] gray_named_but_not_gray_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) gray_named_but_not_gray_src <= 8'b0;
        else gray_named_but_not_gray_src <= data_bus;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) gray_named_but_not_gray_dst <= 8'b0;
        else gray_named_but_not_gray_dst <= gray_named_but_not_gray_src;
    end

endmodule
