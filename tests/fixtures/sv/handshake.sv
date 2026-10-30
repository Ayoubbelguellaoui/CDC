// Handshake payloads. hs_2phase: data and request loaded in one arm, the
// payload captured under the synchronized request (PULP cdc_2phase).
// hs_4phase: the same through next-state nets of an always_comb (PULP
// cdc_4phase). hs_counter: a free-running counter captured under a
// synchronized request it is not loaded with - no handshake, an error.
module hs_2phase (input logic clk_a, input logic clk_b, input logic valid,
                  input logic [7:0] d, output logic [7:0] q);
    logic req, r0, r1, r2;
    logic [7:0] data;
    always_ff @(posedge clk_a)
        if (valid) begin
            req <= ~req;
            data <= d;
        end
    always_ff @(posedge clk_b) begin
        r0 <= req;
        r1 <= r0;
        r2 <= r1;
    end
    always_ff @(posedge clk_b) if (r1 != r2) q <= data;
endmodule

module hs_4phase (input logic clk_a, input logic clk_b, input logic rst_n, input logic valid,
                  input logic [7:0] d, output logic [7:0] q);
    logic req_d, req_q;
    logic [7:0] data_d, data_q;
    logic [1:0] req_sync;
    always_comb begin
        req_d = req_q;
        data_d = data_q;
        if (valid) begin
            req_d = 1'b1;
            data_d = d;
        end
    end
    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) begin
            req_q <= 1'b0;
            data_q <= '0;
        end else begin
            req_q <= req_d;
            data_q <= data_d;
        end
    always_ff @(posedge clk_b) req_sync <= {req_sync[0], req_q};
    always_ff @(posedge clk_b) if (req_sync[1]) q <= data_q;
endmodule

module hs_counter (input logic clk_a, input logic clk_b, input logic valid,
                   output logic [7:0] q);
    logic req, r0, r1, r2;
    logic [7:0] cnt;
    always_ff @(posedge clk_a) cnt <= cnt + 1'b1;
    always_ff @(posedge clk_a) if (valid) req <= ~req;
    always_ff @(posedge clk_b) begin
        r0 <= req;
        r1 <= r0;
        r2 <= r1;
    end
    always_ff @(posedge clk_b) if (r1 != r2) q <= cnt;
endmodule
