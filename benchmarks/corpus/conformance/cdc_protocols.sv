`timescale 1ns/1ps

// CDC conformance: protocol-level synchronization, clk_a -> clk_b.

// Correct toggle pulse synchronizer: a toggle in clk_a, three flops and an
// edge detector in clk_b.
module cf_pulse_toggle (input logic clk_a, input logic clk_b, input logic pulse,
                        output logic q);
    logic tgl;
    logic [2:0] s;
    logic edge_r;
    always_ff @(posedge clk_a) if (pulse) tgl <= ~tgl;
    always_ff @(posedge clk_b) s <= {s[1:0], tgl};
    always_ff @(posedge clk_b) edge_r <= s[2] ^ s[1];
    assign q = edge_r;
endmodule

// Correct 4-phase req/ack handshake carrying an 8-bit payload: req and ack
// through 2FF synchronizers, the payload held stable while req is up.
module cf_handshake4 (input logic clk_a, input logic clk_b, input logic send,
                      input logic [7:0] d, output logic [7:0] q);
    logic req, ack_s0, ack_s1;
    logic [7:0] data_a;
    logic req_s0, req_s1, ack;
    logic [7:0] data_b;
    always_ff @(posedge clk_a) begin
        ack_s0 <= ack;
        ack_s1 <= ack_s0;
    end
    always_ff @(posedge clk_a)
        if (!req && !ack_s1 && send) begin
            data_a <= d;
            req <= 1'b1;
        end else if (req && ack_s1) begin
            req <= 1'b0;
        end
    always_ff @(posedge clk_b) begin
        req_s0 <= req;
        req_s1 <= req_s0;
    end
    always_ff @(posedge clk_b) begin
        ack <= req_s1;
        if (req_s1 && !ack) data_b <= data_a;
    end
    assign q = data_b;
endmodule

// Gray pointer, `x ^ (x >> 1)` in a function, through a 5-bit 2FF chain.
module cf_gray_shift (input logic clk_a, input logic clk_b, input logic inc,
                      output logic [4:0] q);
    function automatic logic [4:0] b2g(logic [4:0] b);
        return b ^ (b >> 1);
    endfunction
    logic [4:0] cnt, ptr_g, s0, s1;
    always_ff @(posedge clk_a) begin
        if (inc) cnt <= cnt + 1'b1;
        ptr_g <= b2g(cnt);
    end
    always_ff @(posedge clk_b) begin
        s0 <= ptr_g;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// Gray pointer built bit by bit, assigned continuously and registered.
module cf_gray_bitwise (input logic clk_a, input logic clk_b, input logic inc,
                        output logic [4:0] q);
    function automatic logic [4:0] b2g(logic [4:0] b);
        logic [4:0] g;
        g[4] = b[4];
        for (int i = 3; i >= 0; i--) g[i] = b[i+1] ^ b[i];
        return g;
    endfunction
    logic [4:0] cnt, cnt_g, ptr_g, s0, s1;
    always_ff @(posedge clk_a) if (inc) cnt <= cnt + 1'b1;
    assign cnt_g = b2g(cnt);
    always_ff @(posedge clk_a) ptr_g <= cnt_g;
    always_ff @(posedge clk_b) begin
        s0 <= ptr_g;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// Binary pointer through the same 2FF chain: unsafe (CDC002).
module cf_ptr_binary (input logic clk_a, input logic clk_b, input logic inc,
                      output logic [4:0] q);
    logic [4:0] cnt, s0, s1;
    always_ff @(posedge clk_a) if (inc) cnt <= cnt + 1'b1;
    always_ff @(posedge clk_b) begin
        s0 <= cnt;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// Gray register decoded to binary before it crosses: unsafe (CDC002).
module cf_gray_decoded (input logic clk_a, input logic clk_b, input logic [4:0] d,
                        output logic [4:0] q);
    logic [4:0] g_r, bin, s0, s1;
    always_ff @(posedge clk_a) g_r <= d ^ (d >> 1);
    assign bin = g_r ^ (g_r >> 1) ^ (g_r >> 2) ^ (g_r >> 3) ^ (g_r >> 4);
    always_ff @(posedge clk_b) begin
        s0 <= bin;
        s1 <= s0;
    end
    assign q = s1;
endmodule

// Data-enable (recirculation mux) synchronizer: a valid flag through a 2FF
// chain, the bus sampled into clk_b only when the synchronized flag says it
// is stable. Correct by protocol.
module cf_data_enable (input logic clk_a, input logic clk_b, input logic load,
                       input logic [7:0] d, output logic [7:0] q);
    logic valid;
    logic [7:0] data_a;
    logic v0, v1, v2;
    logic [7:0] data_b;
    always_ff @(posedge clk_a)
        if (load) begin
            data_a <= d;
            valid <= ~valid;
        end
    always_ff @(posedge clk_b) begin
        v0 <= valid;
        v1 <= v0;
        v2 <= v1;
    end
    always_ff @(posedge clk_b) if (v1 != v2) data_b <= data_a;
    assign q = data_b;
endmodule
