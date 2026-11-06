// Synchronizer styles of real IP. ss_v2001: a Verilog-2001 array shifted by
// `for (i=1; i<N; i=i+1)` with an enable. ss_pulp: PULP common_cells `sync`,
// a packed shift with parameter bounds `{reg_q[STAGES-2:0], d}`.
// ss_gray_enable: a gray pointer captured under an enable.
module ss_v2001 #(parameter N = 2) (input clk_a, input clk_b, input rst_n, input ena,
                                    input d, output q);
    reg a_r;
    reg s [0:N-1];
    integer i;
    always @(posedge clk_a) a_r <= d;
    always @(posedge clk_b or negedge rst_n)
        if (rst_n == 1'b0) begin
            for (i = 0; i < N; i = i + 1) s[i] <= 1'b0;
        end else if (ena == 1'b1) begin
            s[0] <= a_r;
            for (i = 1; i < N; i = i + 1) s[i] <= s[i-1];
        end
    assign q = s[N-1];
endmodule

module ss_pulp #(parameter int unsigned STAGES = 2) (input logic clk_a, input logic clk_b,
                                                    input logic d, output logic q);
    logic a_r;
    logic [STAGES-1:0] reg_q;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) reg_q <= {reg_q[STAGES-2:0], a_r};
    assign q = reg_q[STAGES-1];
endmodule

module ss_gray_enable (input logic clk_a, input logic clk_b, input logic inc,
                       output logic [3:0] q);
    logic [3:0] cnt, cnt_next, g_next, g_r, s0, s1;
    assign cnt_next = cnt + 1'b1;
    assign g_next = (cnt_next >> 1) ^ cnt_next;
    always_ff @(posedge clk_a)
        if (inc) begin
            cnt <= cnt_next;
            g_r <= g_next;
        end
    always_ff @(posedge clk_b) begin
        s0 <= g_r;
        s1 <= s0;
    end
    assign q = s1;
endmodule
