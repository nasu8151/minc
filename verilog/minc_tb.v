`timescale 1ns/1ps

module minc_tb;

    reg CLK;
    reg nRESET;
    wire [15:0] pc_out;
    wire [7:0] acc_out;
    integer i;

    // Instantiate the DUT
    minc uut (
        .CLK(CLK),
        .nRESET(nRESET),
        .pc_out(pc_out),
        .acc_out(acc_out)
    );

    // Clock generator: 10 ns period
    initial begin
        CLK = 0;
        forever #5 CLK = ~CLK;
    end

    // Reset sequence
    initial begin
        nRESET = 0;
        #20;
        nRESET = 1;
    end

    // Waveform dump for Icarus / GTKWave
    initial begin
        $dumpfile("minc_tb.vcd");
        $dumpvars(0, minc_tb);
    end

    // Simple monitor and stop after a number of cycles
    initial begin
        $display("TIME\tPC\tACC");
        // wait for reset to deassert
        @(posedge nRESET);
        // wait a little after reset release
        #1;
        for (i = 0; i < 64; i = i + 1) begin
            @(posedge CLK);
            $display("%0t\t%0h\t%0h", $time, pc_out, acc_out);
        end
        #10;
        $finish;
    end

endmodule
