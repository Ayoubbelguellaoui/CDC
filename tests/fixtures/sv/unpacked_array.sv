// Unpacked arrays. uarr_port: unpacked ports and whole-array assignments
// (getBitWidth() is zero for them: the graph had zero-width nodes).
// uarr_sync: a 2FF synchronizer written as an array, every access a static
// index - one register per element, so the chain is seen. uarr_mixed: one
// read with a variable index - the array stays one node. uarr_param: a
// parameter in an expression is a constant, not a signal.
module uarr_child (
    input  logic [7:0] din [2],
    output logic [7:0] dout[2]
);
    assign dout = din;
endmodule

module uarr_port (
    input  logic       clk,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] in_w [2];
    logic [7:0] out_w[2];
    logic [7:0] mem  [2];
    always_ff @(posedge clk) mem <= '{d, mem[0]};
    assign in_w = mem;
    uarr_child u_child (.din(in_w), .dout(out_w));
    assign q = out_w[1];
endmodule

module uarr_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic d,
    output logic q
);
    logic a_r;
    logic s[2];
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s[0] <= a_r;
        s[1] <= s[0];
    end
    assign q = s[1];
endmodule

module uarr_mixed (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic [1:0] sel,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] rf[4];
    logic [7:0] b_r;
    always_ff @(posedge clk_a) begin
        rf[0] <= d;
        rf[1] <= rf[0];
    end
    always_ff @(posedge clk_b) b_r <= rf[sel];
    assign q = b_r;
endmodule

module uarr_param (
    input  logic       clk,
    input  logic [7:0] d,
    output logic [7:0] q
);
    localparam logic [7:0] MASK = 8'h0f;
    always_ff @(posedge clk) q <= d & MASK;
endmodule
