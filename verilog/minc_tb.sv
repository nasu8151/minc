`timescale 1ns/1ps

`define SIM

`include "ssram.sv"

module minc_tb;

    reg clk;
    reg reset_n;
    wire [7:0] pc_out;
    wire [7:0] top_out;
    wire [7:0] sp_out;
    wire [7:0] address;
    wire [7:0] data_out;
    wire [7:0] data_in;
    wire       we;
    integer i;

    // Instantiate the DUT
    minc uut (
        .clk(clk),
        .reset_n(reset_n),
        .pc_out(pc_out),
        .sp_out(sp_out),
        .address(address),
        .data_out(data_out),
        .we(we),
        .data_in(data_in)
    );

    ssram #(
        .ADDR_WIDTH(8),
        .DATA_WIDTH(8)
    ) data_ram (
        .clk(clk),
        .rst_n(reset_n),
        .addr(address),
        .din(data_out),
        .we(we),
        .dout(data_in),
        .dbg_addr(sp_out),
        .dbg_dout(top_out)
    );

    // Clock generator: 10 ns period
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Reset sequence
    initial begin
        reset_n = 1;
        #1;
        reset_n = 0;
        #20;
        reset_n = 1;
    end

    // `ifndef TEST
    // Waveform dump for Icarus / GTKWave
    initial begin
        $dumpfile("minc_tb.vcd");
        $dumpvars(0, minc_tb);
    end
    // `endif

    // Simple monitor and stop after a number of cycles
    initial begin
        // wait for reset to deassert
        @(posedge reset_n);
        // wait a little after reset release
        #1;
        for (i = 0; i < 65535; i = i + 1) begin
            @(posedge clk);
        end
        #10;
        $display("Timeout reached, finishing simulation.");
        $finish;
    end
    `ifdef VERBOSE
    // Verbose output on each clock cycle
    initial $display("TIME\tPC\tTOP\tSP");
    always @(posedge clk) $display("%0t\t%0h\t%0h\t%0h", $time, pc_out, top_out, sp_out);
    `endif

    final begin
        $display("PC: %0h, TOP: %0h, SP: %0h", pc_out, top_out, sp_out);
    end

endmodule
