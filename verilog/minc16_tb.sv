`timescale 1ns/1ps

`define SIM

`include "ssram16.sv"

// Testbench for minc_16.sv. Kept separate from minc_tb.sv because the minc-16
// bus is 16-bit wide with byte-lane write enables -- the 8-bit testbench cannot
// drive it, and the two cores are not drop-in swappable.
module minc16_tb;

    logic clk;
    logic reset_n;
    logic [15:0] pc_out;
    logic [15:0] top_out;
    logic [15:0] sp_out;
    logic [15:0] address;
    logic [15:0] data_out;
    logic [15:0] data_in;
    logic [15:0] data_in_ram;
    logic [1:0]  we;
    logic        wait_req;
    logic        avma;
    integer i;

    logic [31:0] cycle_count;
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) cycle_count <= 32'd0;
        else          cycle_count <= cycle_count + 32'd1;
    end

    wire ram_ce = address > 16'h000F ? 1'b1 : 1'b0; // RAM is enabled for byte addresses > 0x000F

    logic [7:0] port_a_out;
    logic [7:0] port_a_in = 8'h00;
    logic [7:0] port_a_dir;

    logic [3:0] irq_in;
    `ifdef IRQ_TEST
    integer     irq_cycle;
    integer     irq_mask;
    integer     irq_len;
    integer     irq_period; // 0 = single one-shot pulse; >0 = re-pulse every irq_period cycles
    logic       irq_test_active;
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
    `else
    assign irq_in = 4'b0000;
    `endif

    minc16 uut (
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

    ssram16 #(
        .ADDR_WIDTH(16)
    ) data_ram (
        .clk(clk),
        .rst_n(reset_n),
        .addr(address),
        .din(data_out),
        .we(ram_ce ? we : 2'b00),
        .ce(ram_ce),
        .dout(data_in_ram),
        .dbg_addr0(sp_out),
        .dbg_dout0(top_out)
    );

    // Port A GPIO: byte 0x0004 = output, 0x0005 = direction, 0x0006 = input.
    // 0x0004/0x0005 are the two lanes of word 1... note word address 2.
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            port_a_out <= 8'h00;
            port_a_dir <= 8'h00;
            port_a_in  <= 8'h00;
        end else begin
            if (address[15:1] == 15'h0002) begin // bytes 0x0004 / 0x0005
                if (we[0]) port_a_out <= data_out[7:0]  & port_a_dir;
                if (we[1]) port_a_dir <= data_out[15:8];
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

    assign wait_req = 1'b0;
    assign data_in  = data_in_ram;

    initial begin
        $dumpfile("minc16_tb.vcd");
        $dumpvars(0, minc16_tb);
    end

    initial begin
        @(posedge reset_n);
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
    always @(posedge clk) begin
        if (uut.state == 2'b00) begin // just entered S_FETCH: previous writeback is committed
            $display("PC=%0d\tinsn=%05H\tR0=%4h\tR1=%4h\tR2=%4h\tR3=%4h\tR4=%4h\tR5=%4h\tR6=%4h\tR7=%4h\tR8=%4h\tR9=%4h\tR10=%4h\tR11=%4h\tR12=%4h\tR13=%4h\tR14=%4h\tR15=%4h",
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
