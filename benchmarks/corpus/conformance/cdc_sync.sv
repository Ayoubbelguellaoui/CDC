`timescale 1ns/1ps

// CDC conformance: synchronizer shapes, one top per case, each clk_a -> clk_b.
// Correct designs must give CDC001 info only; broken ones CDC001 error or
// warning (and CDC002 for a bus).

// Unsynchronized single bit.
module cf_unsync_bit (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) q <= a_r;
endmodule

// Unsynchronized 8-bit bus: CDC001 and CDC002.
module cf_unsync_bus (input logic clk_a, input logic clk_b, input logic [7:0] d,
                      output logic [7:0] q);
    logic [7:0] a_r;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) q <= a_r;
endmodule

// One flop only: no synchronizer.
module cf_sync1 (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) s0 <= a_r;
    assign q = s0;
endmodule

// Correct 2FF synchronizer.
module cf_sync2 (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0, s1;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// Correct 3FF synchronizer.
module cf_sync3 (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0, s1, s2;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
        s2 <= s1;
    end
    assign q = s2;
endmodule

// Correct 2FF synchronizer written as one packed vector.
module cf_sync_packed (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r;
    logic [1:0] s;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) s <= {s[0], a_r};
    assign q = s[1];
endmodule

// Correct 2FF synchronizer with an asynchronous reset in the destination.
module cf_sync_reset (input logic clk_a, input logic clk_b, input logic rst_n, input logic d,
                      output logic q);
    logic a_r, s0, s1;
    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) a_r <= 1'b0;
        else a_r <= d;
    always_ff @(posedge clk_b or negedge rst_n)
        if (!rst_n) begin
            s0 <= 1'b0;
            s1 <= 1'b0;
        end else begin
            s0 <= a_r;
            s1 <= s0;
        end
    assign q = s1;
endmodule

// The first, still metastable stage is read through logic by a register.
module cf_tap_logic (input logic clk_a, input logic clk_b, input logic d, input logic e,
                     output logic q);
    logic a_r, b_r, s0, s1, c;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) b_r <= e;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    always_ff @(posedge clk_b) c <= s0 & b_r;
    assign q = c ^ s1;
endmodule

// The first stage drives an output port.
module cf_tap_port (input logic clk_a, input logic clk_b, input logic d, output logic q,
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

// Logic in front of the first stage (two source bits combined).
module cf_logic_before (input logic clk_a, input logic clk_b, input logic d, input logic e,
                        output logic q);
    logic a_r, a2_r, s0, s1;
    always_ff @(posedge clk_a) begin
        a_r <= d;
        a2_r <= e;
    end
    always_ff @(posedge clk_b) begin
        s0 <= a_r ^ a2_r;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// An asynchronous flag used only as an if-condition.
module cf_enable_if (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) if (a_r) q <= 1'b1;
endmodule

// An asynchronous selector used only as a case expression.
module cf_enable_case (input logic clk_a, input logic clk_b, input logic [1:0] d,
                       output logic [3:0] q);
    logic [1:0] a_r;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b)
        case (a_r)
            2'd0: q <= 4'b0001;
            2'd1: q <= 4'b0010;
            2'd2: q <= 4'b0100;
            default: q <= 4'b1000;
        endcase
endmodule

// Correct 2FF synchronizer whose stages have an enable of their own clock.
module cf_sync_enable (input logic clk_a, input logic clk_b, input logic d, input logic e,
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

// A correct 2FF synchronizer followed by a register that uses the synchronized
// value as a condition (an ack toggling on change): that register is no stage.
module cf_sync_consumer (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0, s1, ack;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    always_ff @(posedge clk_b) if (s1 != ack) ack <= ~ack;
    assign q = ack;
endmodule

// A binary bus synchronized bit by bit through a 2FF chain: the bits can
// resolve in different cycles (CDC002).
module cf_bus_bitwise_sync (input logic clk_a, input logic clk_b, input logic [3:0] d,
                            output logic [3:0] q);
    logic [3:0] a_r, s0, s1;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) begin
        s0 <= a_r;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// One source synchronized twice, the two copies recombined: they can disagree
// for a cycle (CDC003 reconvergence).
module cf_reconverge (input logic clk_a, input logic clk_b, input logic d, output logic q);
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

// Same clock, rising and falling edge: one root clock, no crossing.
module cf_same_clock_edges (input logic clk_a, input logic d, output logic q);
    logic p, n;
    always_ff @(posedge clk_a) p <= d;
    always_ff @(negedge clk_a) n <= p;
    assign q = n;
endmodule

// Two correlated sources (a_one is a_r one cycle later), each synchronized on
// its own and recombined at an output port (CDC003).
module cf_reconverge_correlated (input logic clk_a, input logic clk_b, input logic d,
                                 output logic q);
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

// Two unrelated sources synchronized and combined: no relation to lose.
module cf_independent_combined (input logic clk_a, input logic clk_b, input logic d,
                                input logic e, output logic q);
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

// Logic of two source registers in front of a packed 2FF.
module cf_packed_logic (input logic clk_a, input logic clk_b, input logic d, input logic e,
                        output logic q);
    logic a_r, e_r;
    logic [1:0] s;
    always_ff @(posedge clk_a) begin
        a_r <= d;
        e_r <= e;
    end
    always_ff @(posedge clk_b) s <= {s[0], a_r & e_r};
    assign q = s[1];
endmodule

// A 2FF written as a concatenation shift: a synchronizer.
module cf_concat_sync (input logic clk_a, input logic clk_b, input logic d, output logic q);
    logic a_r, s0, s1;
    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) {s1, s0} <= {s0, a_r};
    assign q = s1;
endmodule

// Two registers loaded together from the other clock: both cross.
module cf_concat_unsync (input logic clk_a, input logic clk_b, input logic [1:0] d,
                         output logic [1:0] q);
    logic a0, a1, q0, q1;
    always_ff @(posedge clk_a) {a1, a0} <= d;
    always_ff @(posedge clk_b) {q1, q0} <= {a1, a0};
    assign q = {q1, q0};
endmodule
