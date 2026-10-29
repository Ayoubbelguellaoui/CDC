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
