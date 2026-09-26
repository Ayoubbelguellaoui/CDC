`timescale 1ns/1ps

// Hierarchical design: three levels, crossings happen at module
// boundaries so hierarchical register names must survive intact.
//   1. Clean path (TRACED): top-level clk_a register -> 2FF island
//      (clk_b) -> consumer. Reported as CDC001 info/verified_safe.
//   2. Gap path (NOT traced — known limitation): the clk_a status flop
//      lives inside hier_sender, so its path to the clk_b consumer
//      (sender_status -> q_raw_ff, no synchronizer) crosses an
//      instance-output -> wire -> instance-input boundary that the
//      frontend does not trace. No finding is emitted even though the
//      RTL has a real unsynchronized crossing here. Encoded as an
//      ambiguous control (see manifest + known gaps).

module hier_sender (
    input  wire       clk_a,
    input  wire       rst_n,
    input  wire [3:0] status_in,
    output wire       status_out
);
    reg status_ff;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) status_ff <= 1'b0;
        else status_ff <= ^status_in;
    end

    assign status_out = status_ff;
endmodule

module hier_sync_island (
    input  wire clk_b,
    input  wire rst_n,
    input  wire din_in,
    output wire din_out
);
    reg meta_ff;
    reg sync_ff;

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            meta_ff <= 1'b0;
            sync_ff <= 1'b0;
        end else begin
            meta_ff <= din_in;
            sync_ff <= meta_ff;
        end
    end

    assign din_out = sync_ff;
endmodule

module hier_consumer (
    input  wire clk_b,
    input  wire rst_n,
    input  wire synced_din,
    input  wire raw_status,
    output wire q_sync,
    output wire q_raw
);
    reg q_sync_ff;
    reg q_raw_ff;

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            q_sync_ff <= 1'b0;
            q_raw_ff  <= 1'b0;
        end else begin
            q_sync_ff <= synced_din;
            // BUG (invisible to the tool): raw_status comes straight from
            // the clk_a domain inside hier_sender, with no synchronizer.
            q_raw_ff <= raw_status;
        end
    end

    assign q_sync = q_sync_ff;
    assign q_raw  = q_raw_ff;
endmodule

module hierarchical_soc (
    input  wire       clk_a,
    input  wire       clk_b,
    input  wire       rst_n,
    input  wire       din,
    input  wire [3:0] status_in,
    output wire       q_sync,
    output wire       q_raw
);
    reg  topo_src_clean;
    wire sender_status;
    wire island_out;

    // Top-level source register: parent-reg -> child-input traces fine.
    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) topo_src_clean <= 1'b0;
        else topo_src_clean <= din;
    end

    hier_sender u_sender (
        .clk_a      (clk_a),
        .rst_n      (rst_n),
        .status_in  (status_in),
        .status_out (sender_status)
    );

    hier_sync_island u_island (
        .clk_b   (clk_b),
        .rst_n   (rst_n),
        .din_in  (topo_src_clean),
        .din_out (island_out)
    );

    hier_consumer u_consumer (
        .clk_b      (clk_b),
        .rst_n      (rst_n),
        .synced_din (island_out),
        .raw_status (sender_status),
        .q_sync     (q_sync),
        .q_raw      (q_raw)
    );
endmodule
