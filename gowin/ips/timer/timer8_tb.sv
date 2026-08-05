`timescale 1ns/1ps

module timer8_tb;

    logic       I_CLK;
    logic       I_RESETN;
    logic       I_TX_EN;
    logic [2:0] I_WADDR;
    logic [7:0] I_WDATA;
    logic       I_RX_EN;
    logic [2:0] I_RADDR;
    logic [7:0] O_RDATA;
    logic       O_OVERFLOW;
    logic       O_COMPARE;
    logic       O_OVF_INT;
    logic       O_CMP_INT;

    TIMER8 dut (
        .I_CLK     (I_CLK),
        .I_RESETN  (I_RESETN),
        .I_TX_EN   (I_TX_EN),
        .I_WADDR   (I_WADDR),
        .I_WDATA   (I_WDATA),
        .I_RX_EN   (I_RX_EN),
        .I_RADDR   (I_RADDR),
        .O_RDATA   (O_RDATA),
        .O_OVERFLOW(O_OVERFLOW),
        .O_COMPARE (O_COMPARE),
        .O_OVF_INT (O_OVF_INT),
        .O_CMP_INT (O_CMP_INT)
    );

    initial begin
        I_CLK = 1'b0;
        forever #5 I_CLK = ~I_CLK;
    end

    int pass_count = 0;
    int fail_count = 0;

    task automatic check(input logic cond, input string msg);
        if (cond) begin
            pass_count++;
            $display("[OK] %s", msg);
        end else begin
            fail_count++;
            $display("[FAIL] %s", msg);
        end
    endtask

    task automatic bus_write(input logic [2:0] addr, input logic [7:0] data);
        begin
            @(negedge I_CLK);
            I_WADDR = addr;
            I_WDATA = data;
            I_TX_EN = 1'b1;
            @(negedge I_CLK);
            I_TX_EN = 1'b0;
        end
    endtask

    task automatic bus_read(input logic [2:0] addr, output logic [7:0] data);
        begin
            @(negedge I_CLK);
            I_RADDR = addr;
            I_RX_EN = 1'b1;
            @(negedge I_CLK); // one posedge occurred in between: O_RDATA now valid
            I_RX_EN = 1'b0;
            data = O_RDATA;
        end
    endtask

    // Icarus Verilog doesn't support `ref` task ports, so each signal we need to
    // wait on gets its own task instead of a generic one parameterized by ref.

    task wait_pulse_compare(input int timeout_cycles, output logic got);
        begin
            got = 1'b0;
            fork
                begin : SIGWAIT
                    @(posedge O_COMPARE);
                    got = 1'b1;
                end
                begin : TMOWAIT
                    repeat (timeout_cycles) @(posedge I_CLK);
                end
            join_any
            disable SIGWAIT;
            disable TMOWAIT;
        end
    endtask

    logic [7:0] counter_at_overflow;

    task wait_pulse_overflow(input int timeout_cycles, output logic got);
        begin
            got = 1'b0;
            fork
                begin : SIGWAIT
                    @(posedge O_OVERFLOW);
                    got = 1'b1;
                    // sample the internal counter the instant the pulse fires -- the
                    // timer keeps free-running afterwards, so reading it back over the
                    // register bus a few cycles later would no longer show the reload.
                    counter_at_overflow = dut.counter_reg;
                end
                begin : TMOWAIT
                    repeat (timeout_cycles) @(posedge I_CLK);
                end
            join_any
            disable SIGWAIT;
            disable TMOWAIT;
        end
    endtask

    task wait_level_cmp_int(input logic level, input int timeout_cycles, output logic got);
        begin
            got = 1'b0;
            fork
                begin : SIGWAIT
                    wait (O_CMP_INT === level);
                    got = 1'b1;
                end
                begin : TMOWAIT
                    repeat (timeout_cycles) @(posedge I_CLK);
                end
            join_any
            disable SIGWAIT;
            disable TMOWAIT;
        end
    endtask

    task wait_level_ovf_int(input logic level, input int timeout_cycles, output logic got);
        begin
            got = 1'b0;
            fork
                begin : SIGWAIT
                    wait (O_OVF_INT === level);
                    got = 1'b1;
                end
                begin : TMOWAIT
                    repeat (timeout_cycles) @(posedge I_CLK);
                end
            join_any
            disable SIGWAIT;
            disable TMOWAIT;
        end
    endtask

    logic [7:0] rdata;
    logic       got;

    initial begin
        $dumpfile("timer8_tb.vcd");
        $dumpvars(0, timer8_tb);

        I_RESETN = 1'b1;
        I_TX_EN  = 1'b0;
        I_RX_EN  = 1'b0;
        I_WADDR  = 3'h0;
        I_WDATA  = 8'h0;
        I_RADDR  = 3'h0;
        #1;
        I_RESETN = 1'b0;
        #20;
        I_RESETN = 1'b1;
        @(negedge I_CLK);

        // 1. reset defaults
        bus_read(3'h0, rdata); check(rdata == 8'h00, "CONFIG resets to 0");
        bus_read(3'h1, rdata); check(rdata == 8'h00, "COMPARE resets to 0");
        bus_read(3'h2, rdata); check(rdata == 8'h00, "OVERFLOW resets to 0");
        bus_read(3'h3, rdata); check(rdata == 8'h00, "COUNTER resets to 0");
        bus_read(3'h4, rdata); check(rdata == 8'h00, "STATUS resets to 0");
        check(O_OVF_INT == 1'b0 && O_CMP_INT == 1'b0, "interrupt outputs low after reset");

        // 2. write/read roundtrip on every register (EN stays 0, so the timer can't tick yet)
        bus_write(3'h1, 8'hA5); bus_read(3'h1, rdata); check(rdata == 8'hA5, "COMPARE write/read roundtrip");
        bus_write(3'h2, 8'h5A); bus_read(3'h2, rdata); check(rdata == 8'h5A, "OVERFLOW write/read roundtrip");
        bus_write(3'h3, 8'h11); bus_read(3'h3, rdata); check(rdata == 8'h11, "COUNTER write/read roundtrip (preset)");
        bus_write(3'h3, 8'h00); // put COUNTER back to 0 before the counting tests below

        // 3+4. prescale=/1 (sel=000), OVERFLOW=10, COMPARE=5, EN=1, IE_OVF=1, IE_CMP=1
        bus_write(3'h1, 8'd5);
        bus_write(3'h2, 8'd10);
        bus_write(3'h0, 8'b000_1_1_1); // [5:3]=000(/1) IE_CMP=1 IE_OVF=1 EN=1

        wait_pulse_compare(30, got);
        check(got, "O_COMPARE pulses once COUNTER reaches COMPARE (5)");
        bus_read(3'h4, rdata);
        check(rdata[1] == 1'b1, "STATUS.CMP set after compare match");
        wait_level_cmp_int(1'b1, 5, got);
        check(got, "O_CMP_INT asserted (STATUS.CMP & IE_CMP)");

        // 5. clear the compare interrupt via STATUS write-1-to-clear
        bus_write(3'h4, 8'b0000_0010);
        wait_level_cmp_int(1'b0, 5, got);
        check(got, "O_CMP_INT deasserts after clearing STATUS.CMP");
        bus_read(3'h4, rdata);
        check(rdata[1] == 1'b0, "STATUS.CMP reads back 0 after clear");

        // 6. COUNTER reaches OVERFLOW (10): reloads to 0, O_OVERFLOW pulses, STATUS.OVF/O_OVF_INT set
        wait_pulse_overflow(30, got);
        check(got, "O_OVERFLOW pulses once COUNTER reaches OVERFLOW (10)");
        bus_read(3'h4, rdata);
        check(rdata[0] == 1'b1, "STATUS.OVF set after overflow match");
        wait_level_ovf_int(1'b1, 5, got);
        check(got, "O_OVF_INT asserted (STATUS.OVF & IE_OVF)");
        check(counter_at_overflow == 8'h00, "COUNTER reloaded to 0 on the overflow cycle");

        bus_write(3'h4, 8'b0000_0001);
        wait_level_ovf_int(1'b0, 5, got);
        check(got, "O_OVF_INT deasserts after clearing STATUS.OVF");

        // stop the timer before the next sub-tests
        bus_write(3'h0, 8'h00);
        bus_write(3'h3, 8'h00);
        bus_write(3'h4, 8'hFF); // clear any stale pending flags

        // 7. prescaler divides the tick rate: sel=010 (/4) should count much slower than /1
        bus_write(3'h1, 8'hFF); // put COMPARE out of reach so it doesn't interfere
        bus_write(3'h2, 8'hFF); // put OVERFLOW out of reach so it doesn't interfere
        bus_write(3'h0, 8'b010_0_0_1); // [5:3]=010(/4), IE_CMP=0, IE_OVF=0, EN=1
        bus_read(3'h3, rdata);
        check(rdata == 8'h00, "COUNTER starts at 0 before prescale-/4 measurement");
        repeat (4) @(posedge I_CLK);
        bus_read(3'h3, rdata);
        check(rdata <= 8'h01, "COUNTER has ticked at most once after 4 clocks at /4 prescale");
        repeat (16) @(posedge I_CLK);
        bus_read(3'h3, rdata);
        check(rdata >= 8'h03 && rdata <= 8'h06, "COUNTER advanced ~4 counts after 20 total clocks at /4 prescale");

        // 8. EN=0 freezes the counter
        bus_write(3'h0, 8'h00);
        bus_read(3'h3, rdata);
        begin
            logic [7:0] frozen_at;
            frozen_at = rdata;
            repeat (20) @(posedge I_CLK);
            bus_read(3'h3, rdata);
            check(rdata == frozen_at, "COUNTER stays frozen while EN=0");
        end

        $display("==========================================");
        $display("timer8_tb: %0d passed, %0d failed", pass_count, fail_count);
        if (fail_count == 0) $display("[SUMMARY] ALL TESTS PASSED");
        else $display("[SUMMARY] SOME TESTS FAILED");
        $finish;
    end

endmodule
