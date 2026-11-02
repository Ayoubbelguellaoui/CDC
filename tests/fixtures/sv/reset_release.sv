// The release of asynchronous resets (CDC014, reset_policy.check_reset_release).
// rr_raw: a port reset straight into a register - a finding.
// rr_sync: the port reset through a two-stage reset synchronizer - none.
// rr_logic: two port resets combined in logic, unsynchronized - a finding.
// rr_two_clk: synchronized for clk_a only, clk_b registers on the raw
// reset - a finding for clk_b, none for clk_a.
module rr_raw (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) q <= 1'b0;
        else q <= d;
endmodule

module rr_sync (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    logic rst_meta, rst_sync;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) begin
            rst_meta <= 1'b0;
            rst_sync <= 1'b0;
        end else begin
            rst_meta <= 1'b1;
            rst_sync <= rst_meta;
        end
    always_ff @(posedge clk or negedge rst_sync)
        if (!rst_sync) q <= 1'b0;
        else q <= d;
endmodule

module rr_logic (
    input  logic clk,
    input  logic rst_a_n,
    input  logic rst_b_n,
    input  logic d,
    output logic q
);
    logic rst_n;
    assign rst_n = rst_a_n & rst_b_n;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) q <= 1'b0;
        else q <= d;
endmodule

module rr_two_clk (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic d,
    output logic qa,
    output logic qb
);
    logic rst_meta, rst_sync;
    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) begin
            rst_meta <= 1'b0;
            rst_sync <= 1'b0;
        end else begin
            rst_meta <= 1'b1;
            rst_sync <= rst_meta;
        end
    always_ff @(posedge clk_a or negedge rst_sync)
        if (!rst_sync) qa <= 1'b0;
        else qa <= d;
    always_ff @(posedge clk_b or negedge rst_n)
        if (!rst_n) qb <= 1'b0;
        else qb <= d;
endmodule
