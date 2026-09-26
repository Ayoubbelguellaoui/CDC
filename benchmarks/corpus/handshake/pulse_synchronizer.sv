`timescale 1ns/1ps

// Correct design: single-bit pulse (strobe) crossing via toggle + 2FF +
// edge-detect. The source toggles pulse_toggle for every input strobe;
// the toggle crosses through two destination-domain flops and an XOR
// edge detector regenerates exactly one destination-clock pulse.
// Expected: no CDC011 (the toggle path uses a full 2FF chain); the
// CDC001 crossing is info/verified_safe.

module pulse_synchronizer (
    input  wire src_clk,
    input  wire dst_clk,
    input  wire rst_n,
    input  wire strobe_in,
    output wire pulse_out
);

    reg pulse_toggle;
    reg pulse_sync1;
    reg pulse_sync2;

    // Source: convert pulse to a level toggle.
    always @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) pulse_toggle <= 1'b0;
        else if (strobe_in) pulse_toggle <= ~pulse_toggle;
    end

    // Destination: 2FF + edge detect regenerates the pulse.
    always @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            pulse_sync1 <= 1'b0;
            pulse_sync2 <= 1'b0;
        end else begin
            pulse_sync1 <= pulse_toggle;
            pulse_sync2 <= pulse_sync1;
        end
    end

    assign pulse_out = pulse_sync1 ^ pulse_sync2;

endmodule
