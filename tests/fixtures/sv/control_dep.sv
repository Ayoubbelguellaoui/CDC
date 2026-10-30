// Control dependencies and synchronizer stage taps.
// cd_enable: an asynchronous flag used only as an enable is a crossing.
// cd_sync_enable: a 2FF chain with an enable of its own clock is still a
// chain. cd_gated_consumer: a register that depends on the last stage only
// through a condition (a 2-phase handshake ack) is no third stage.
// cd_tap_logic / cd_tap_port: the first, still metastable stage read through
// logic or by an output port - the chain gets a structural warning.
module cd_enable (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) if (a_r) q <= 1'b1;
endmodule

module cd_sync_enable (input logic clk_a, input logic clk_b, input logic d, input logic e,
                       output logic q);
    logic a_r, en_b, s0, s1;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) en_b <= e;
    always_ff @(posedge clk_b)
        if (en_b) begin
            s0 <= a_r;
            s1 <= s0;
        end
    assign q = s1;
endmodule

module cd_gated_consumer (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0, s1, ack;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    always_ff @(posedge clk_b) if (s1 != ack) ack <= ~ack;
    assign q = ack;
endmodule

module cd_tap_logic (input logic clk_a, input logic clk_b, input logic d, input logic e,
                     output logic q);
    logic a_r, b_r, s0, s1, c;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) b_r <= e;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    always_ff @(posedge clk_b) c <= s0 ^ b_r;
    assign q = c ^ s1;
endmodule

module cd_tap_port (input logic clk_a, input logic clk_b, input logic d, output logic q,
                    output logic early);
    logic a_r, s0, s1;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    assign q = s1;
    assign early = s0;
endmodule

// cd_stage2_condition: the second stage is used as a condition (a handshake
// ack copies it, a capture is enabled by it): the synchronized output, no
// fanout hazard.
module cd_stage2_condition (input logic clk_a, input logic clk_b, input logic d,
                            input logic [7:0] e, output logic [7:0] q);
    logic a_r, s0, s1, ack;
    logic [7:0] b_r;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    always_ff @(posedge clk_b) begin
        ack <= s1;
        if (s1 && !ack) b_r <= e;
    end
    assign q = b_r;
endmodule

// cd_sync_twice: one source synchronized twice, the copies recombined - the
// reconvergence hazard. cd_sync_once: synchronized once, the output fanned
// out - safe.
module cd_sync_twice (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0a, s1a, s0b, s1b, c;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0a <= a_r;
        s1a <= s0a;
        s0b <= a_r;
        s1b <= s0b;
    end
    always_ff @(posedge clk_b) c <= s1a ^ s1b;
    assign q = c;
endmodule

module cd_sync_once (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0, s1, c0, c1, c;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    always_ff @(posedge clk_b) begin
        c0 <= s1;
        c1 <= ~s1;
        c <= c0 ^ c1;
    end
    assign q = c;
endmodule

// cd_correlated: a_one is a_r one cycle later; both synchronized on their
// own and recombined at an output port - their relation is lost.
// cd_independent: two unrelated sources synchronized and combined - no
// relation to lose.
module cd_correlated (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, a_one;
    logic [1:0] s_r, s_one;
    always_ff @(posedge clk_a) begin
        a_r <= d;
        a_one <= a_r;
    end
    always_ff @(posedge clk_b) begin
        s_r <= {s_r[0], a_r};
        s_one <= {s_one[0], a_one};
    end
    assign q = s_r[1] ^ s_one[1];
endmodule

module cd_independent (input logic clk_a, input logic clk_b, input logic d, input logic e,
                       output logic q);
    logic a_r, a_s;
    logic [1:0] s_r, s_s;
    always_ff @(posedge clk_a) begin
        a_r <= d;
        a_s <= e;
    end
    always_ff @(posedge clk_b) begin
        s_r <= {s_r[0], a_r};
        s_s <= {s_s[0], a_s};
    end
    assign q = s_r[1] ^ s_s[1];
endmodule
