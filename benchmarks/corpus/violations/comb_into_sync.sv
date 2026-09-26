`timescale 1ns/1ps

// Known violation (CDC006-class): combinational logic sits between the
// cross-domain feed and the first synchronizer stage. The AND gate
// re-converges an unrelated clk_b-domain mask with the asynchronous
// input, so the "meta" flop samples a glitch-prone combination rather
// than the raw crossing signal.
// [CORPUS NOTE — verified 2026-09-26] Reported as CDC001 error on
// src_ff -> meta_ff (plus CDC013 on sync_ff); CDC006 does NOT fire even
// though comb feeds a sync stage. Root cause: Cdc006Analyzer needs the
// chain entry to have a DIRECT cross-domain register predecessor
// (register_predecessors(entry, /*traverse=*/false)), but any comb in the
// feed path removes that direct edge — and the same comb breaks the sync
// chain classification itself. So comb anywhere in the sync feed surfaces
// as CDC001, never CDC006. Documented as an ambiguous control: the design
// IS buggy, only the rule attribution differs. See manifest + CDC006
// known gap.

module comb_into_sync (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire data_in,
    input  wire mask_in,
    output wire data_out
);

    reg src_ff;
    reg mask_ff;
    reg meta_ff;
    reg sync_ff;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) src_ff <= 1'b0;
        else src_ff <= data_in;
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) mask_ff <= 1'b0;
        else mask_ff <= mask_in;
    end

    // BUG: combinational AND between the async feed and a local mask
    // directly drives the first sync stage.
    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta_ff <= 1'b0;
            sync_ff <= 1'b0;
        end else begin
            meta_ff <= src_ff & mask_ff;
            sync_ff <= meta_ff;
        end
    end

    assign data_out = sync_ff;

endmodule
