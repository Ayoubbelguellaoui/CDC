`timescale 1ns/1ps

// Gray-coded crossings where the bin2gray transform lives inside
// subroutines: plain function call, nested function calls, and task with
// output arg. The frontend must attribute the XOR structure through each
// call boundary so CDC002 does not fire on any of them.

module gray_func_crossing (
    input  wire       clk_a,
    input  wire       clk_b,
    input  wire       rst_n,
    input  wire [3:0] bin_in
);

    reg [3:0] gray_src;
    reg [3:0] gray_dst_ff1;
    reg [3:0] gray_dst_ff2;

    reg [3:0] nest_src;
    reg [3:0] nest_ff1;
    reg [3:0] nest_ff2;

    reg [3:0] task_src;
    reg [3:0] task_ff1;
    reg [3:0] task_ff2;

    function [3:0] bin2gray;
        input [3:0] b;
        begin
            bin2gray = b ^ (b >> 1);
        end
    endfunction

    function [3:0] outer_gray;
        input [3:0] b;
        begin
            outer_gray = inner_gray(b);
        end
    endfunction

    function [3:0] inner_gray;
        input [3:0] b;
        begin
            inner_gray = b ^ (b >> 1);
        end
    endfunction

    task gray_task;
        input [3:0] b;
        output [3:0] g;
        begin
            g = b ^ (b >> 1);
        end
    endtask

    reg [3:0] task_g;

    always @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) begin
            gray_src <= 4'b0;
            nest_src <= 4'b0;
            task_src <= 4'b0;
        end else begin
            gray_src <= bin2gray(bin_in);
            nest_src <= outer_gray(bin_in);
            gray_task(bin_in, task_g);
            task_src <= task_g;
        end
    end

    always @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            gray_dst_ff1 <= 4'b0;
            gray_dst_ff2 <= 4'b0;
            nest_ff1 <= 4'b0;
            nest_ff2 <= 4'b0;
            task_ff1 <= 4'b0;
            task_ff2 <= 4'b0;
        end else begin
            gray_dst_ff1 <= gray_src;
            gray_dst_ff2 <= gray_dst_ff1;
            nest_ff1 <= nest_src;
            nest_ff2 <= nest_ff1;
            task_ff1 <= task_src;
            task_ff2 <= task_ff1;
        end
    end

endmodule
