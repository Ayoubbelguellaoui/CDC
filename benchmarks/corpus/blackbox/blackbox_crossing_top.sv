`timescale 1ns/1ps

// Vendor CDC IP used as a black box: the synchronizer lives inside
// vendor_cdc_bit (e.g. Xilinx XPM_CDC_SINGLE), whose RTL is not part of
// this design. The crossing top-level register -> blackbox -> destination
// register must be reported WITHOUT the vendor YAML config, and
// suppressed WITH it (see vendor_cdc_ip.yaml + the A/B manifest entries).

module vendor_cdc_bit (
    input  wire src_clk,
    input  wire src_in,
    input  wire dest_clk,
    output wire dest_out
);
    // Opaque safe-macro MODEL: the real macro hides hardened sync cells.
    // The internal 2FF stands in for the hidden transfer function (same
    // role as a Liberty .lib black box): WITHOUT the vendor YAML the tool
    // reports CDC001 info (sync chain seen) + CDC007 info (no reset
    // inside the macro); WITH vendor_cdc_ip.yaml (is_safe_crossing) the
    // CDC001 becomes a "safe black box" suppressed/audit finding and the
    // CDC007 disappears. See the A/B manifest entries, which tell the two
    // CDC001 variants apart with reason_contains.
    reg v_meta;
    reg v_sync;

    always @(posedge dest_clk) begin
        v_meta <= src_in;
        v_sync <= v_meta;
    end

    assign dest_out = v_sync;
endmodule

module blackbox_crossing_top (
    input  wire clk_a,
    input  wire clk_b,
    input  wire rst_n,
    input  wire data_in,
    output wire data_out
);
    reg bb_src;
    wire bb_out_wire;
    reg bb_dst;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) bb_src <= 1'b0;
        else bb_src <= data_in;
    end

    vendor_cdc_bit u_vendor_sync (
        .src_clk  (clk_a),
        .src_in   (bb_src),
        .dest_clk (clk_b),
        .dest_out (bb_out_wire)
    );

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) bb_dst <= 1'b0;
        else bb_dst <= bb_out_wire;
    end

    assign data_out = bb_dst;
endmodule
