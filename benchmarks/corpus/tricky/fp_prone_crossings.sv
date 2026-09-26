`timescale 1ns/1ps

// Tricky cases that stress false-positive / false-negative behavior.
//   1. Quasi-static configuration bit: written once after reset, never
//      changes during operation, but crosses with no synchronizer.
//      Structurally this is an unsynchronized crossing (the tool flags
//      it); functionally it is usually benign. Documented as AMBIGUOUS.
//   2. Constant tie-off: a hard 1'b1 sampled across domains. No real
//      metastability risk, but there is no data register on the source
//      side at all. Documented as AMBIGUOUS.
//   3. Asynchronous-assert, synchronous-deassert reset synchronizer:
//      the standard safe pattern. Must be QUIET (negative control).

module fp_prone_crossings (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire cfg_in,
    input  wire data_in,
    output wire cfg_out,
    output wire tie_out,
    output wire data_out,
    output wire mx_out
);

    // --- Case 1: quasi-static config (ambiguous) ---
    reg cfg_src;
    reg cfg_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) cfg_src <= 1'b0;
        else cfg_src <= cfg_in;  // In practice: written once at boot.
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) cfg_dst <= 1'b0;
        else cfg_dst <= cfg_src;
    end

    // --- Case 2: constant tie-off sampled across domains (ambiguous) ---
    reg tie_dst;

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) tie_dst <= 1'b0;
        else tie_dst <= 1'b1;
    end

    // --- Case 3: reset synchronizer + data under the COMMON reset ---
    // The async reset is fanned through a clk_b reset synchronizer
    // (async assert via rst_n, synchronous deassert); the data path is a
    // plain 2FF chain under rst_n on both sides.
    // Must NOT be flagged: single reset domain, proper synchronizer.
    reg arst_meta;
    reg arst_sync;
    reg arst_data_src;
    reg arst_data_meta;
    reg arst_data_dst;

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            arst_meta <= 1'b0;
            arst_sync <= 1'b0;
        end else begin
            arst_meta <= 1'b1;
            arst_sync <= arst_meta;
        end
    end

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) arst_data_src <= 1'b0;
        else arst_data_src <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            arst_data_meta <= 1'b0;
            arst_data_dst  <= 1'b0;
        end else begin
            arst_data_meta <= arst_data_src;
            arst_data_dst  <= arst_data_meta;
        end
    end

    // --- Case 4: MIXED reset domains (ambiguous, review-worthy) ---
    // Same 2FF data path, but the destination flops use the synchronized
    // reset arst_sync while the source uses rst_n. CDC001 is info (proper
    // 2FF), but CDC009 flags the reset-domain difference as an error.
    // The reset IS synchronized (arst chain above), yet the tool reports
    // it: reset-synchronizer suppression only applies once the
    // destination sits 2+ data stages deep in the new reset domain
    // (ResetDomainAnalyzer::has_reset_synchronizer). Whether this should
    // be quiet is a judgment call — encoded as ambiguous.
    reg mx_data_src;
    reg mx_data_meta;
    reg mx_data_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) mx_data_src <= 1'b0;
        else mx_data_src <= data_in;
    end

    always @(posedge clk_b or negedge arst_sync) begin
        if (!arst_sync) begin
            mx_data_meta <= 1'b0;
            mx_data_dst  <= 1'b0;
        end else begin
            mx_data_meta <= mx_data_src;
            mx_data_dst  <= mx_data_meta;
        end
    end

    assign cfg_out  = cfg_dst;
    assign tie_out  = tie_dst;
    assign data_out = arst_data_dst;
    assign mx_out   = mx_data_dst;

endmodule
