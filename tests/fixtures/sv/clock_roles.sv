// Clock and reset of an event list by structure: the event the leading if
// tests is the asynchronous control. The resets here fail the name heuristic
// (camelCase, trst_ni, rstb). cr_inv_mux: an inverted and a bypassed copy of
// one clock through a clock mux - one root.
module cr_roles (input logic IwClockIn, input logic IwAsyncRstIn_an, input logic tck,
                 input logic trst_ni, input logic clk, input logic rstb, input logic d,
                 output logic q1, output logic q2, output logic q3);
    always_ff @(posedge IwClockIn or negedge IwAsyncRstIn_an)
        if (!IwAsyncRstIn_an) q1 <= 1'b0;
        else q1 <= d;
    always_ff @(posedge tck or negedge trst_ni)
        if (!trst_ni) q2 <= 1'b0;
        else q2 <= d;
    always @(posedge clk or negedge rstb) begin : blk
        integer i;
        if (rstb == 1'b0) q3 <= 1'b0;
        else q3 <= d;
    end
endmodule

module cr_inverter (input logic clk_i, output logic clk_o);
    assign clk_o = ~clk_i;
endmodule

module cr_clk_mux (input logic clk0_i, input logic clk1_i, input logic sel, output logic clk_o);
    assign clk_o = sel ? clk1_i : clk0_i;
endmodule

module cr_inv_mux (input logic tck, input logic test, input logic d, output logic q);
    logic tck_ni, tck_n, a_r;
    cr_inverter u_inv (.clk_i(tck), .clk_o(tck_ni));
    cr_clk_mux u_mux (.clk0_i(tck_ni), .clk1_i(tck), .sel(test), .clk_o(tck_n));
    always_ff @(posedge tck) a_r <= d;
    always_ff @(posedge tck_n) q <= a_r;
endmodule
