// Shapes of a real SerDes design (two async FIFOs, PLL clocks).
// fs_loop_sync: a synchronizer whose stages are an unpacked array shifted by
// a procedural for loop (`s[i] <= s[i-1]`). fs_gray_ptr: a gray pointer
// built bit by bit in a function (`g[i] = b[i+1] ^ b[i]`), assigned
// continuously, registered and synchronized by a 5-bit chain.
// fs_gray_decoded: the same gray register decoded to binary before it
// crosses. fs_bin_ptr: a binary pointer through the same chain.
// fs_pll: clocks out of a black-box PLL (undriven outputs) through a
// clock buffer into a submodule.
module fs_sync #(
    parameter int STAGES = 2,
    parameter int W      = 1
) (
    input  logic         clk,
    input  logic [W-1:0] d,
    output logic [W-1:0] q
);
    logic [W-1:0] s[STAGES];
    always_ff @(posedge clk) begin
        s[0] <= d;
        for (int i = 1; i < STAGES; i++) s[i] <= s[i-1];
    end
    assign q = s[STAGES-1];
endmodule

module fs_loop_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic d,
    output logic q
);
    logic a_r;
    always_ff @(posedge clk_a) a_r <= d;
    fs_sync u_sync (.clk(clk_b), .d(a_r), .q(q));
endmodule

module fs_gray_ptr (
    input  logic       clk_a,
    input  logic       clk_b,
    output logic [4:0] q
);
    function automatic logic [4:0] b2g(logic [4:0] b);
        logic [4:0] g;
        g[4] = b[4];
        for (int i = 3; i >= 0; i--) g[i] = b[i+1] ^ b[i];
        return g;
    endfunction
    logic [4:0] cnt_b, cnt_g, cnt_g_r;
    always_ff @(posedge clk_a) cnt_b <= cnt_b + 1'b1;
    assign cnt_g = b2g(cnt_b);
    always_ff @(posedge clk_a) cnt_g_r <= cnt_g;
    fs_sync #(.W(5)) u_sync (.clk(clk_b), .d(cnt_g_r), .q(q));
endmodule

module fs_gray_decoded (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic [4:0] d,
    output logic [4:0] q
);
    logic [4:0] g_r;
    logic [4:0] bin;
    always_ff @(posedge clk_a) g_r <= d ^ (d >> 1);
    assign bin = g_r ^ (g_r >> 1) ^ (g_r >> 2) ^ (g_r >> 3) ^ (g_r >> 4);
    fs_sync #(.W(5)) u_sync (.clk(clk_b), .d(bin), .q(q));
endmodule

module fs_bin_ptr (
    input  logic       clk_a,
    input  logic       clk_b,
    output logic [4:0] q
);
    logic [4:0] cnt_b;
    always_ff @(posedge clk_a) cnt_b <= cnt_b + 1'b1;
    fs_sync #(.W(5)) u_sync (.clk(clk_b), .d(cnt_b), .q(q));
endmodule

module fs_pll_model (
    input  logic clk_in,
    output logic clk0,
    output logic clk1
);
endmodule

module fs_buf (
    input  logic i,
    output logic o
);
    assign o = i;
endmodule

module fs_pll (
    input  logic clk_ref,
    input  logic d,
    output logic q
);
    logic clk0_raw, clk1_raw, clk0, clk1;
    logic a_r;
    fs_pll_model u_pll (.clk_in(clk_ref), .clk0(clk0_raw), .clk1(clk1_raw));
    fs_buf u_buf0 (.i(clk0_raw), .o(clk0));
    fs_buf u_buf1 (.i(clk1_raw), .o(clk1));
    always_ff @(posedge clk0) a_r <= d;
    fs_sync u_sync (.clk(clk1), .d(a_r), .q(q));
endmodule
