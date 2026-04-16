`timescale 1ns/1ps

`define SIM

`include "ssram.sv"

module minc_tb;

    logic clk;
    logic reset_n;
    logic [15:0] pc_out;
    logic [7:0] top_out;
    logic [15:0] sp_out;
    logic [15:0] address;
    logic [7:0] data_out;
    logic [7:0] data_in;
    logic [7:0] data_in_ram;
    logic       we;
    logic        wait_req;
    logic        wait_rel;
    integer i;

    wire ram_ce = address > 16'h000F ? 1'b1 : 1'b0; // RAM is enabled for addresses > 0x000F

    logic [7:0] port_a_out;
    logic [7:0] port_a_in = 8'h00; // Initialize port A input to 0
    logic [7:0] port_a_dir;

    // Instantiate the DUT
    minc uut (
        .clk(clk),
        .reset_n(reset_n),
        .pc_out(pc_out),
        .sp_out(sp_out),
        .address(address),
        .data_out(data_out),
        .we(we),
        .data_in(data_in),
        .wait_req(wait_req),
        .wait_rel(wait_rel)
    );

    ssram #(
        .ADDR_WIDTH(16),
        .DATA_WIDTH(8)
    ) data_ram (
        .clk(clk),
        .rst_n(reset_n),
        .addr(address[7:0]),
        .din(data_out),
        .we(we),
        .ce(ram_ce),
        .dout(data_in_ram),
        .dbg_addr(sp_out[7:0]),
        .dbg_dout(top_out)
    );

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            port_a_out <= 8'h00;
            port_a_dir <= 8'h00; // All inputs by default
            port_a_in <= 8'h00;
        end else begin
            // port_a_in <= port_a_out; // Loopback for testing
            if (address == 16'h0000) begin
                // data_in <= port_a_out; // Read from port A
                if (we) port_a_out <= data_out & port_a_dir; // Output only on bits set as output
            end else if (address == 16'h0001) begin
                // data_in <= port_a_dir; // Read direction register
                if (we) port_a_dir <= data_out; // Set direction on address 0x01
            end else if (address == 16'h0002) begin
                // data_in <= port_a_in & ~port_a_dir; // Read input values on bits set as input
            end
        end
    end

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

    `ifdef WAIT_TEST
    parameter WAITp3 = 6;
    logic [WAITp3:0] wait_sr;
    assign wait_req = wait_sr[1];
    assign wait_rel = wait_sr[WAITp3 - 1];
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            wait_sr <= 'b1;
        end else begin
            if (address[15:3] == 13'b00001) begin
                {wait_sr[0], wait_sr[WAITp3:1]} <= wait_sr;
            end else
                wait_sr <= 'b1;
        end
    end
    assign data_in = (address == 16'h000D) ? 8'h01 : data_in_ram;
    `else
    assign wait_req = 1'b0;
    assign wait_rel = 1'b0;
    assign data_in = data_in_ram;
    `endif

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
        for (i = 0; i < 131071; i = i + 1) begin
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
        $display("PORTA: %0h", port_a_out);
        $display("PC: %0h, TOP: %0h, SP: %0h", pc_out, top_out, sp_out);
    end

endmodule
