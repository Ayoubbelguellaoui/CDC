// =============================================================================
// OpenCDC Stress-Test System — UART-to-AXI Bridge Subsystem
//
// 4 clock domains, 8 modules, exercises all CDC rules (CDC001-CDC008).
//
// Domains:
//   clk_uart  (115200 baud) — UART interface
//   clk_bridge (100 MHz)    — Bridge logic
//   clk_axi   (200 MHz)     — AXI master
//   clk_slow  (50 MHz)      — Slow peripheral
// =============================================================================

module stress_test_system (
    input  logic clk_uart,
    input  logic clk_bridge,
    input  logic clk_axi,
    input  logic clk_slow,
    input  logic rst_n,

    // UART interface
    input  logic uart_rx_pin,
    output logic uart_tx_pin,

    // AXI interface
    output logic [31:0] axi_addr,
    output logic [31:0] axi_wdata,
    output logic        axi_valid,
    input  logic        axi_ready,

    // Control
    input  logic        bridge_en,
    input  logic        clk_mux_sel,
    input  logic [7:0]  test_data_in,
    output logic [7:0]  test_data_out,
    output logic        test_data_valid
);

    // =========================================================================
    // Internal wires
    // =========================================================================
    logic [7:0] rx_data;
    logic       rx_valid;
    logic [7:0] bridge_to_axi_data;
    logic       bridge_to_axi_valid;
    logic [7:0] axi_to_slow_data;
    logic       axi_to_slow_valid;
    logic [7:0] gray_to_bridge_data;
    logic       gray_to_bridge_valid;
    logic [7:0] pipeline_data;
    logic       pipeline_valid;

    // =========================================================================
    // 1. UART RX — CDC001, CDC007
    //    Crosses clk_uart -> clk_bridge WITHOUT synchronization.
    //    No reset signal on RX domain registers.
    // =========================================================================
    uart_rx u_uart_rx (
        .clk_uart   (clk_uart),
        .uart_rx_pin(uart_rx_pin),
        .rx_data    (rx_data),
        .rx_valid   (rx_valid)
    );

    // =========================================================================
    // 2. 2FF Sync Bus — CDC002, CDC004
    //    8-bit bus crosses clk_bridge -> clk_axi.
    //    Gated clock on source side (bridge_en).
    //    No gray-code, no handshake => CDC002 fires.
    // =========================================================================
    sync_2ff_bus u_sync_2ff_bus (
        .clk_src       (clk_bridge),
        .clk_src_en    (bridge_en),
        .clk_dst       (clk_axi),
        .rst_n         (rst_n),
        .data_in       (rx_data),
        .valid_in      (rx_valid),
        .data_out      (bridge_to_axi_data),
        .valid_out     (bridge_to_axi_valid)
    );

    // =========================================================================
    // 3. Gray Counter — NO CDC002 (gray-code suppresses it)
    //    8-bit gray-coded counter crossing clk_axi -> clk_slow.
    //    Name contains "gray" => CDC002 suppressed.
    // =========================================================================
    gray_counter u_gray_counter (
        .clk_src  (clk_axi),
        .clk_dst  (clk_slow),
        .rst_n    (rst_n),
        .count_in (bridge_to_axi_data),
        .count_out(gray_to_bridge_data),
        .valid_out(gray_to_bridge_valid)
    );

    // =========================================================================
    // 4. Reconvergence Block — CDC003
    //    Single source fans out to dst1 and dst2, reconverge at consumer.
    // =========================================================================
    reconvergence_block u_reconvergence (
        .clk_a     (clk_bridge),
        .clk_b     (clk_axi),
        .rst_n     (rst_n),
        .src_data  (bridge_to_axi_data),
        .src_valid (bridge_to_axi_valid),
        .consumer  (axi_to_slow_data),
        .consumer_valid (axi_to_slow_valid)
    );

    // =========================================================================
    // 5. Clock Mux Control — CDC005
    //    Muxed clock without reset.
    // =========================================================================
    clock_mux_ctrl u_clock_mux (
        .clk_a      (clk_bridge),
        .clk_b      (clk_axi),
        .rst_n      (rst_n),
        .sel        (clk_mux_sel),
        .data_in    (bridge_to_axi_data),
        .valid_in   (bridge_to_axi_valid),
        .data_out   (axi_wdata[7:0]),
        .valid_out  (axi_valid)
    );

    // =========================================================================
    // 6. Pipeline Daisy — CDC008
    //    4-stage pipeline crossing all 4 clock domains.
    // =========================================================================
    pipeline_daisy u_pipeline (
        .clk1       (clk_uart),
        .clk2       (clk_bridge),
        .clk3       (clk_axi),
        .clk4       (clk_slow),
        .rst_n      (rst_n),
        .data_in    (test_data_in),
        .data_out   (pipeline_data),
        .valid_out  (pipeline_valid)
    );

    // =========================================================================
    // 7. Handshake Crossing — NO CDC002 (valid/ready suppresses it)
    //    Uses valid/ready protocol => CDC002 suppressed.
    // =========================================================================
    handshake_crossing u_handshake (
        .clk_src    (clk_bridge),
        .clk_dst    (clk_axi),
        .rst_n      (rst_n),
        .src_data   (axi_to_slow_data),
        .src_valid  (axi_to_slow_valid),
        .dst_data   (axi_wdata[31:24]),
        .dst_valid  (),
        .dst_ready  (axi_ready)
    );

    // =========================================================================
    // 8. Resetless Flops — CDC007
    //    Registers without any reset signal.
    // =========================================================================
    resetless_flops u_resetless (
        .clk_a      (clk_bridge),
        .clk_b      (clk_axi),
        .data_in    (gray_to_bridge_data),
        .data_out   (test_data_out)
    );

    assign axi_addr = 32'h0000_0000;

endmodule


// =============================================================================
// Module: uart_rx
// Rules: CDC001 (unsynchronized crossing), CDC007 (no reset)
// =============================================================================
module uart_rx (
    input  logic       clk_uart,
    input  logic       uart_rx_pin,
    output logic [7:0] rx_data,
    output logic       rx_valid
);
    logic [7:0] shift_reg;
    logic [3:0] bit_cnt;
    logic       sampling;

    always_ff @(posedge clk_uart) begin
        if (!sampling && !uart_rx_pin) begin
            sampling <= 1'b1;
            bit_cnt  <= 4'd0;
        end else if (sampling) begin
            if (bit_cnt < 4'd8) begin
                shift_reg <= {uart_rx_pin, shift_reg[7:1]};
                bit_cnt   <= bit_cnt + 1'b1;
            end else begin
                rx_data   <= shift_reg;
                rx_valid  <= 1'b1;
                sampling  <= 1'b0;
            end
        end else begin
            rx_valid <= 1'b0;
        end
    end

endmodule


// =============================================================================
// Module: sync_2ff_bus
// Rules: CDC002 (multi-bit crossing), CDC004 (gated clock)
// 8-bit bus with gated source clock, no gray-code.
// =============================================================================
module sync_2ff_bus (
    input  logic       clk_src,
    input  logic       clk_src_en,
    input  logic       clk_dst,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    input  logic       valid_in,
    output logic [7:0] data_out,
    output logic       valid_out
);
    logic       clk_src_gated;
    logic [7:0] src_reg;
    logic [7:0] meta_reg;
    logic [7:0] dst_reg;
    logic       src_valid;
    logic       meta_valid;
    logic       dst_valid_r;

    assign clk_src_gated = clk_src & clk_src_en;

    always_ff @(posedge clk_src_gated or negedge rst_n) begin
        if (!rst_n) begin
            src_reg  <= 8'b0;
            src_valid <= 1'b0;
        end else begin
            src_reg  <= data_in;
            src_valid <= valid_in;
        end
    end

    always_ff @(posedge clk_dst or negedge rst_n) begin
        if (!rst_n) begin
            meta_reg  <= 8'b0;
            meta_valid <= 1'b0;
            dst_reg   <= 8'b0;
            dst_valid_r <= 1'b0;
        end else begin
            meta_reg  <= src_reg;
            meta_valid <= src_valid;
            dst_reg   <= meta_reg;
            dst_valid_r <= meta_valid;
        end
    end

    assign data_out  = dst_reg;
    assign valid_out = dst_valid_r;

endmodule


// =============================================================================
// Module: gray_counter
// No CDC002 — name contains "gray".
// =============================================================================
module gray_counter (
    input  logic       clk_src,
    input  logic       clk_dst,
    input  logic       rst_n,
    input  logic [7:0] count_in,
    output logic [7:0] count_out,
    output logic       valid_out
);
    logic [7:0] gray_src;
    logic [7:0] gray_dst;
    logic [7:0] bin_out;

    always_ff @(posedge clk_src or negedge rst_n) begin
        if (!rst_n)
            gray_src <= 8'b0;
        else
            gray_src <= count_in ^ (count_in >> 1);
    end

    always_ff @(posedge clk_dst or negedge rst_n) begin
        if (!rst_n) begin
            gray_dst  <= 8'b0;
            valid_out <= 1'b0;
        end else begin
            gray_dst  <= gray_src;
            valid_out <= 1'b1;
        end
    end

    assign bin_out = gray_dst ^ (gray_dst >> 1) ^ (gray_dst >> 2) ^ (gray_dst >> 3);
    assign count_out = bin_out;

endmodule


// =============================================================================
// Module: reconvergence_block
// Rules: CDC003 (reconvergence hazard)
// Single source fans out to dst1 and dst2, reconverge at consumer.
// =============================================================================
module reconvergence_block (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic       rst_n,
    input  logic [7:0] src_data,
    input  logic       src_valid,
    output logic [7:0] consumer,
    output logic       consumer_valid
);
    logic [7:0] src_ff;
    logic [7:0] dst1_ff;
    logic [7:0] dst2_ff;
    logic [7:0] consumer_ff;
    logic       consumer_valid_r;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            src_ff <= 8'b0;
        else
            src_ff <= src_data;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            dst1_ff <= 8'b0;
            dst2_ff <= 8'b0;
        end else begin
            dst1_ff <= src_ff;
            dst2_ff <= src_ff;
        end
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            consumer_ff     <= 8'b0;
            consumer_valid_r <= 1'b0;
        end else begin
            consumer_ff     <= dst1_ff + dst2_ff;
            consumer_valid_r <= 1'b1;
        end
    end

    assign consumer       = consumer_ff;
    assign consumer_valid = consumer_valid_r;

endmodule


// =============================================================================
// Module: clock_mux_ctrl
// Rules: CDC005 (muxed clock without reset)
// =============================================================================
module clock_mux_ctrl (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic       rst_n,
    input  logic       sel,
    input  logic [7:0] data_in,
    input  logic       valid_in,
    output logic [7:0] data_out,
    output logic       valid_out
);
    logic       clk_muxed;
    logic [7:0] mux_reg;
    logic       mux_valid;

    assign clk_muxed = sel ? clk_b : clk_a;

    always_ff @(posedge clk_muxed) begin
        mux_reg   <= data_in;
        mux_valid <= valid_in;
    end

    logic [7:0] crossing_reg;
    logic       crossing_valid;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            crossing_reg  <= 8'b0;
            crossing_valid <= 1'b0;
        end else begin
            crossing_reg  <= mux_reg;
            crossing_valid <= mux_valid;
        end
    end

    assign data_out  = crossing_reg;
    assign valid_out = crossing_valid;

endmodule


// =============================================================================
// Module: pipeline_daisy
// Rules: CDC008 (multi-domain daisy chain)
// 4-stage pipeline crossing all 4 clock domains.
// =============================================================================
module pipeline_daisy (
    input  logic       clk1,
    input  logic       clk2,
    input  logic       clk3,
    input  logic       clk4,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out,
    output logic       valid_out
);
    logic [7:0] stage1;
    logic [7:0] stage2;
    logic [7:0] stage3;
    logic [7:0] stage4;
    logic       v1, v2, v3, v4;

    always_ff @(posedge clk1 or negedge rst_n) begin
        if (!rst_n) begin
            stage1 <= 8'b0;
            v1     <= 1'b0;
        end else begin
            stage1 <= data_in;
            v1     <= 1'b1;
        end
    end

    always_ff @(posedge clk2 or negedge rst_n) begin
        if (!rst_n) begin
            stage2 <= 8'b0;
            v2     <= 1'b0;
        end else begin
            stage2 <= stage1;
            v2     <= v1;
        end
    end

    always_ff @(posedge clk3 or negedge rst_n) begin
        if (!rst_n) begin
            stage3 <= 8'b0;
            v3     <= 1'b0;
        end else begin
            stage3 <= stage2;
            v3     <= v2;
        end
    end

    always_ff @(posedge clk4 or negedge rst_n) begin
        if (!rst_n) begin
            stage4 <= 8'b0;
            v4     <= 1'b0;
        end else begin
            stage4 <= stage3;
            v4     <= v3;
        end
    end

    assign data_out  = stage4;
    assign valid_out = v4;

endmodule


// =============================================================================
// Module: handshake_crossing
// No CDC002 — names contain "valid" and "ready".
// =============================================================================
module handshake_crossing (
    input  logic       clk_src,
    input  logic       clk_dst,
    input  logic       rst_n,
    input  logic [7:0] src_data,
    input  logic       src_valid,
    output logic [7:0] dst_data,
    output logic       dst_valid,
    input  logic       dst_ready
);
    logic [7:0] src_reg;
    logic       src_valid_r;
    logic       handshake_done;

    always_ff @(posedge clk_src or negedge rst_n) begin
        if (!rst_n) begin
            src_reg      <= 8'b0;
            src_valid_r  <= 1'b0;
            handshake_done <= 1'b0;
        end else begin
            if (src_valid && !handshake_done) begin
                src_reg      <= src_data;
                src_valid_r  <= 1'b1;
            end else if (dst_ready && src_valid_r) begin
                src_valid_r  <= 1'b0;
                handshake_done <= 1'b1;
            end else if (!src_valid) begin
                handshake_done <= 1'b0;
            end
        end
    end

    logic [7:0] dst_reg;
    logic       dst_valid_r;

    always_ff @(posedge clk_dst or negedge rst_n) begin
        if (!rst_n) begin
            dst_reg    <= 8'b0;
            dst_valid_r <= 1'b0;
        end else begin
            dst_reg    <= src_reg;
            dst_valid_r <= src_valid_r;
        end
    end

    assign dst_data  = dst_reg;
    assign dst_valid = dst_valid_r;

endmodule


// =============================================================================
// Module: resetless_flops
// Rules: CDC007 (missing reset)
// No reset signal on any register.
// =============================================================================
module resetless_flops (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    logic [7:0] reg_a;
    logic [7:0] reg_b;

    always_ff @(posedge clk_a) begin
        reg_a <= data_in;
    end

    always_ff @(posedge clk_b) begin
        reg_b <= reg_a;
    end

    assign data_out = reg_b;

endmodule
