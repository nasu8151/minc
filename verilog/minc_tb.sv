`timescale 1ns/1ps

`define SIM

`include "ssram.sv"

module minc_tb;

    logic clk;
    logic reset_n;
    logic [15:0] pc_out;
    logic [15:0] top_out;
    logic [15:0] sp_out;
    logic [15:0] address;
    logic [7:0] data_out;
    logic [7:0] data_in;
    logic [7:0] data_in_ram;
    logic       we;
    logic       wait_req;
    logic       avma;
    logic       wait_ma;
    integer i;

    logic [31:0] cycle_count;
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) cycle_count <= 32'd0;
        else          cycle_count <= cycle_count + 32'd1;
    end

    wire ram_ce = address > 16'h000F ? 1'b1 : 1'b0; // RAM is enabled for addresses > 0x000F

    logic [7:0] port_a_out;
    logic [7:0] port_a_in = 8'h00; // Initialize port A input to 0
    logic [7:0] port_a_dir;

    // minc_h.sv-only irq_in port. Guarded so this same testbench still builds
    // against minc_p2.sv/minc_p5.sv (via tests/test_pipeline.py), which don't
    // have this port yet.
    `ifdef IRQ_TEST
    logic [3:0] irq_in;
    integer     irq_cycle;
    integer     irq_mask;
    integer     irq_len;
    integer     irq_period; // 0 = single one-shot pulse (legacy behavior); >0 = pulse recurs every irq_period cycles
    integer     irq_cur;
    logic       irq_test_active;
    logic       fire;
    initial begin
        irq_test_active = $value$plusargs("irq_cycle=%d", irq_cycle);
        if (!$value$plusargs("irq_mask=%d", irq_mask)) irq_mask = 1;
        if (!$value$plusargs("irq_len=%d", irq_len)) irq_len = 6;
        if (!$value$plusargs("irq_period=%d", irq_period)) irq_period = 0;
    end
    assign irq_in = (irq_test_active && (cycle_count >= irq_cycle) &&
                     (irq_period > 0
                        ? (((cycle_count - irq_cycle) % irq_period) < irq_len)
                        : (cycle_count < irq_cycle + irq_len)))
                    ? irq_mask[3:0] : 4'b0000;
    `endif

    // Instantiate the DUT
    `ifdef IRQ_TEST
    minc uut (
        .clk(clk),
        .reset_n(reset_n),
        .pc_out(pc_out),
        .sp_out(sp_out),
        .address(address),
        .data_out(data_out),
        .we(we),
        .avma(avma),
        .data_in(data_in),
        .wait_req(wait_req),
        .irq_in(irq_in)
    );
    `else
    minc uut (
        .clk(clk),
        .reset_n(reset_n),
        .pc_out(pc_out),
        .sp_out(sp_out),
        .address(address),
        .data_out(data_out),
        .we(we),
        .avma(avma),
        .data_in(data_in),
        .wait_req(wait_req)
    );
    `endif

    ssram #(
        .ADDR_WIDTH(16),
        .DATA_WIDTH(8)
    ) data_ram (
        .clk(clk),
        .rst_n(reset_n),
        .addr(address),
        .din(data_out),
        .we(we),
        .ce(ram_ce),
        .dout(data_in_ram),
        .dbg_addr0(sp_out),
        .dbg_dout0(top_out[7:0]),
        .dbg_addr1(sp_out + 16'h0001),
        .dbg_dout1(top_out[15:8]),
        .dbg_addr2(16'hFFFB)
    );

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            port_a_out <= 8'h00;
            port_a_dir <= 8'h00; // All inputs by default
            port_a_in <= 8'h00;
        end else begin
            // port_a_in <= port_a_out; // Loopback for testing
            if (address == 16'h0004) begin
                // data_in <= port_a_out; // Read from port A
                if (we) port_a_out <= data_out & port_a_dir; // Output only on bits set as output
            end else if (address == 16'h0005) begin
                // data_in <= port_a_dir; // Read direction register
                if (we) port_a_dir <= data_out; // Set direction on address 0x01
            end else if (address == 16'h0006) begin
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
    // assign wait_req = 8'h10 > address ? 1'b1 : 1'b0;
    parameter WAITp4 = 8;
    logic [WAITp4:0] wait_sr;
    assign wait_req = (wait_sr[WAITp4-2:1] != 'b0) ? 1'b1 : 1'b0;
    assign wait_ma = wait_sr[1];
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            wait_sr <= 'b1;
        end else begin
            if ((address[15:3] == 13'b00001) && avma) begin
                {wait_sr[0], wait_sr[WAITp4:1]} <= wait_sr;
            end else
                wait_sr <= 'b1;
        end
    end
    assign data_in = (address == 16'h000D) ? 8'h21 : data_in_ram;
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
            if (uut.pc === 16'hxxxx) begin
                $display("[ERROR] PC == xxxx. finishing simulation");
                $finish;
            end
        end
        #10;
        $display("Timeout reached, finishing simulation.");
        $finish;
    end
    `ifdef VERBOSE
    // Verbose output on each clock cycle
    always @(posedge clk) begin
        if (uut.state == 2'b00) begin // just entered S_FETCH: previous instruction's writeback is committed
            $display("PC=%0d\tinsn=%05H\tR0=%2h\tR1=%2h\tR2=%2h\tR3=%2h\tR4=%2h\tR5=%2h\tR6=%2h\tR7=%2h\tR8=%2h\tR9=%2h\tR10=%2h\tR11=%2h\tR12=%2h\tR13=%2h\tR14=%2h\tR15=%2h",
                pc_out, uut.instr,
                uut.regs[0], uut.regs[1], uut.regs[2], uut.regs[3],
                uut.regs[4], uut.regs[5], uut.regs[6], uut.regs[7],
                uut.regs[8], uut.regs[9], uut.regs[10], uut.regs[11],
                uut.regs[12], uut.regs[13], uut.regs[14], uut.regs[15]);
        end
    end
    `endif

    final begin
        $display("PORTA: %0h", port_a_out);
        $display("PC: %0h, TOP: %0h, SP: %0h", pc_out, top_out, sp_out);
        $display("CYCLES: %0d", cycle_count);
    end

endmodule
