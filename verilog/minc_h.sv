`define S_FETCH   2'b00
`define S_DECEXEC 2'b01
`define S_MA      2'b10
`define S_WB      2'b11

module minc (
    input  logic        clk,
    input  logic        reset_n,
    output logic [15:0] pc_out,
    output logic [15:0] sp_out,
    output logic [15:0] address,
    output logic [7:0]  data_out,
    output logic        we,
    output logic        avma,
    input  logic [7:0]  data_in,
    input  logic        wait_req,
    input  logic [3:0]  irq_in
);

    // PC, SP

    localparam ROM_ADDR_WIDTH = 12;
    localparam RAM_ADDR_WIDTH = 12;

    logic [ROM_ADDR_WIDTH - 1:0] pc;
    logic [15:0] sp;

    logic [1:0] state;
    logic servicing_irq; // 1 for the 4-state hardware interrupt-entry pseudo-op (push + jump to IRQ_VECTOR)
    logic [2:0] irq_sel; // which irq_in line was accepted (0 = highest priority), latched on entry

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15];
    // GPRs (upper)
    logic  [7:0]  regs_hi [0:7];

    // Instruction ROM: sized to fit 4 pROMX9 blocks (4096 x 18-bit).
    // pc stays 16-bit for pc_out/branch arithmetic, but only its low 12
    // bits address the ROM so the synthesizer doesn't need to prove pc's
    // reachable range to keep this out of LUT-based fallback.
    logic  [17:0] rom  [0:(1<<ROM_ADDR_WIDTH)-1]; /* synthesis syn_romstyle = "BLOCK_ROM" */
    wire [17:0] cur = rom[pc[ROM_ADDR_WIDTH-1:0]];

    // ROM load (one word per line, hex). TEST selects test.hex
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    // Outputs
    assign pc_out  = pc;
    assign sp_out  = sp;

    logic [17:0] instr;

    // PSR: bit0 = carry, bit1 = ie (interrupt enable), bit2-7 reserved (read as 0).
    // Memory-mapped at 0x0002 (read/write via existing stm/ldm absolute addressing).
    logic [1:0] psr;
    logic [1:0] psr_shadow; // one-level auto-save of psr, memory-mapped at 0x0003, restored by RETI
    wire        carry_flag = psr[0];
    wire        ie         = psr[1];
    logic carry_flag_next;

    wire [5:0] op6 = instr[17:12];
    wire [3:0] op4 = instr[17:14];
    wire [1:0] op2 = instr[17:16];
    wire [3:0] subop = instr[13:10];

    wire [3:0] rd = instr[7:4];
    wire [3:0] rs = instr[3:0];

    // imm8 is the low half of every immediate in the ISA -- mvi, decs, jz, and
    // the low byte of the calr/jr word all read these same bits, which is what
    // lets branch_ofs below share one 8-bit path.
    wire  [7:0] imm8  = {instr[11:8], instr[3:0]};
    // Absolute stm/ldm address is {m, n} with m = instr[3:0] as the HIGH nibble.
    wire  [9:0] imm10 = {instr[3:0], instr[13:8]};
    wire signed [15:0] ofs6 = 16'($signed(instr[13:8]));        // rp+n displacement

    // wire [7:0] rd_val = regs[rd];
    // wire [7:0] rs_val = regs[rs];

    wire [7:0] ra_val = regs[rd];
    wire [7:0] rb_val = regs[rs];

    logic [7:0] alu_out;

    // Opcode map (see Hardware.md):
    //   0000xx  ALU group (subop = instr[13:10])   0001xx  free
    //   0010xx  immediate group: mvi / decs / jz / free
    //   0011xx  stack group:     ret / reti / push / pop
    //   01....  memory group     10....  calr      11....  jr (halt = jr -1)
    // Every class falls out of the top bits, so each field is decoded once
    // instead of one 6-bit comparator per mnemonic.
    wire is_alu = (op4 == 4'b0000);

    wire is_imm_grp = (op6[5:2] == 4'b0010);
    wire is_mvi     = is_imm_grp && (op6[1:0] == 2'b00);
    wire is_decs    = is_imm_grp && (op6[1:0] == 2'b01);
    wire is_jz      = is_imm_grp && (op6[1:0] == 2'b10);

    wire is_stack_grp = (op6[5:2] == 4'b0011);
    wire is_ret_insn  = is_stack_grp && (op6[1]   == 1'b0);  // ret + reti
    wire is_reti      = is_stack_grp && (op6[1:0] == 2'b01);
    wire is_push      = is_stack_grp && (op6[1:0] == 2'b10);
    wire is_pop       = is_stack_grp && (op6[1:0] == 2'b11);

    // stm/ldm share op2==01; op6[3] picks the addressing mode (register pair
    // vs. absolute) and op6[2] picks load vs. store.
    wire is_mem_grp = (op2 == 2'b01);
    wire is_stm     = is_mem_grp && (op6[2] == 1'b0);
    wire is_ldm     = is_mem_grp && (op6[2] == 1'b1);
    wire is_addr_rp = is_mem_grp && (op6[3] == 1'b0);
    wire is_addr_n  = is_mem_grp && (op6[3] == 1'b1);

    wire is_calr   = (op2 == 2'b10);
    wire is_jr     = (op2 == 2'b11);

    // ALU
    // ADD/ADC/SUB/SBC/LT share the adder below: alu_subop[1] selects subtract
    // (invert b, cin defaults to 1), alu_subop[0] selects carry-in from the
    // flag (ADC/SBC). LT reads out the borrow instead of the sum.
    // mvi is executed as "0 + imm8" on the shared adder rather than as its own
    // arm on the writeback mux, so its subop is forced to ADD and alu_out then
    // selects group1. Nothing else has to change: mvi is not is_alu, so it
    // neither drives add_cin's ALU arm nor writes the carry flag.
    wire [3:0]  alu_subop  = is_mvi ? 4'b0100 : subop;
    wire        alu_do_sub = alu_subop[1];
    wire        alu_use_cf = alu_subop[0];
    wire  [7:0] alu_b      = alu_do_sub ? ~rb_val : rb_val;
    wire        alu_cin    = alu_use_cf ? carry_flag : alu_do_sub;

    // ---------------------------------------------------------------------
    // Shared 16-bit add/sub datapath (ALU low byte + AGU)
    // ---------------------------------------------------------------------
    // In this 4-state machine no instruction needs the ALU and the AGU at the
    // same time: the ALU group (op4==0000) touches neither memory nor SP, and
    // every memory/stack/call class leaves the ALU idle. So one carry chain
    // serves both, selected by instruction class. It is written as two 8-bit
    // halves so the ALU can take the byte carry-out (add_c8) for the C flag
    // while the AGU takes the full 16-bit sum.
    //
    // The PC adder stays separate: calr needs pc+1 and sp-1 in the same state.
    //
    // servicing_irq is the first arm of every mux because instr still holds the
    // (not executed) instruction that was fetched when the interrupt was taken,
    // so is_alu/is_addr_rp/... can be spuriously true during the entry pseudo-op.
    wire [7:0] rp_hi = regs_hi[rs[3:1]];  // odd half of the pair; low half is rb_val

    wire [15:0] add_a = servicing_irq ? sp                :
                        is_alu        ? {8'h00, ra_val}   :
                        is_addr_rp    ? {rp_hi, rb_val}   :
                        is_mvi        ? 16'h0000          : sp;

    // mvi and decs share one immediate path. mincasm already encodes decs with
    // ~n (the inverter lives there so it costs no LUTs here), so the low byte is
    // literally the same imm8 for both and only the sign extension differs:
    // mvi adds {8'h00, imm8} to zero, decs adds {8'hFF, ~n} + 1 to SP.
    wire [15:0] add_b = servicing_irq        ? 16'hFFFF               :
                        is_alu               ? {8'h00, alu_b}         :
                        is_addr_rp           ? ofs6                   :
                        (is_mvi || is_decs)  ? {{8{is_decs}}, imm8}   :
                        (is_calr || is_push) ? 16'hFFFF               : 16'h0000;

    wire add_cin = servicing_irq ? 1'b0     :
                   is_alu        ? alu_cin  :
                                   (is_decs || is_pop || is_ret_insn);

    wire       add_c8, add_c16;
    wire [7:0] add_lo, add_hi;
    assign {add_c8,  add_lo} = {1'b0, add_a[7:0]}  + {1'b0, add_b[7:0]}  + add_cin;
    assign {add_c16, add_hi} = {1'b0, add_a[15:8]} + {1'b0, add_b[15:8]} + add_c8;
    wire [15:0] add_out = {add_hi, add_lo};

    wire        alu_cout = add_c8;
    wire  [7:0] group1   = add_lo;

    // MOV/OR/AND/XOR: one LUT4 per bit (two data inputs, two selects).
    logic [7:0] group0;
    generate
        for (genvar i=0;i<8;i++) begin
            assign group0[i] =  (alu_subop[1:0] == 2'b00) ? rb_val[i] :
                                (alu_subop[1:0] == 2'b01) ? ra_val[i] | rb_val[i] :
                                (alu_subop[1:0] == 2'b10) ? ra_val[i] & rb_val[i] :
                                (alu_subop[1:0] == 2'b11) ? ra_val[i] ^ rb_val[i] : 1'bx;
        end
    endgenerate

    // alu_subop[3:1]: 3'b101 -> lt/ltc, 3'b100 -> rr.
    //
    // rr is a rotate right *through* the carry -- {rd, c} = {c, rs} -- so the
    // new rd is {carry_flag, rb_val[7:1]} and the bit rotated out is rb_val[0].
    // The original code wrote {carry_flag, rb_val}: nine bits into an eight-bit
    // target, so the top bit was silently truncated, the shift never happened,
    // and rr just returned rb_val. Hence the slice here -- without it the
    // instruction assembles and executes but does nothing useful.
    wire [7:0] group2 = alu_subop[1] ? {7'b0, ~alu_cout}           // lt / ltc
                                     : {carry_flag, rb_val[7:1]};  // rr

    // The 8'hxx / 1'bx arms below are load-bearing, not laziness: they are the
    // unused opcode slots, and leaving them explicitly undefined is what lets
    // the synthesizer share this mux with the writeback path. Replacing them
    // with concrete values measured ~6 LUTs *worse*.
    wire [7:0] group3 = (alu_subop[1:0] == 2'b00) ? {7'b0, ~|ra_val} :
                        (alu_subop[1:0] == 2'b10) ? ra_val * rb_val  :
                        (alu_subop[1:0] == 2'b11) ? (16'(ra_val * rb_val)) >> 8 : 8'hxx;

    always_comb begin
        case (alu_subop[3:2])
            2'b00: begin alu_out = group0; carry_flag_next = 1'bx;      end // MOV/OR/AND/XOR
            2'b01: begin alu_out = group1; carry_flag_next = alu_cout;  end // ADD/ADC/SUB/SBC
            // rb_val[0] is exactly the bit rr rotates out. lt/ltc share it,
            // although Hardware.md specifies !borrow for those -- see 既知の制約.
            2'b10: begin alu_out = group2; carry_flag_next = rb_val[0]; end // RR/LT/LTC
            2'b11: begin alu_out = group3; carry_flag_next = 1'bx;      end // CHZ/MUL/MULH
            default: begin alu_out = 8'hxx; carry_flag_next = 1'bx;     end
        endcase
    end


    // State machine
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state <= `S_FETCH;
        end else begin
            case (state)
                `S_FETCH: begin
                    state <= `S_DECEXEC;
                end
                `S_DECEXEC: begin
                    state <= `S_MA;
                end
                `S_MA: begin
                    state <= `S_WB;
                end
                `S_WB: begin
                    if (!servicing_irq && (is_stm || is_ldm) && wait_req) state <= `S_WB;
                    else state <= `S_FETCH;
                end
                default: state <= `S_FETCH;
            endcase
        end
    end
    assign we = (servicing_irq && (state == `S_MA || state == `S_WB))
                || ((is_calr || is_stm || is_push) && (state == `S_WB))
                || ((is_calr) && (state == `S_MA)) ? 1'b1 : 1'b0;
    assign avma = !servicing_irq && (is_stm || is_ldm) && (state == `S_MA || state == `S_WB);

    // Interrupt entry: 4 level-triggered request lines, fixed priority (irq_in[0] highest).
    // Vectors live in PC-space (rom[]/pc), entirely separate from the 0x0002/0x0003 data-space
    // MMIO addresses used by PSR/psr_shadow below.
    wire any_irq = |irq_in;
    wire [2:0] irq_sel_next =   irq_in[0] ? 3'd1 :
                                irq_in[1] ? 3'd2 :
                                irq_in[2] ? 3'd3 :
                                irq_in[3] ? 3'd4 : 3'dx;
    wire take_irq = ie && any_irq;
    wire [ROM_ADDR_WIDTH-1:0] irq_vector = {13'd0, irq_sel};

    // PC and ROM control
    //
    // jz and jr/calr take their low offset byte from the very same instruction
    // bits ({instr[11:8], instr[3:0]} == imm8), so only the high byte needs a
    // mux: jz sign-extends it, jr/calr take the second immediate half. That
    // turns what was a 16-bit three-way mux into an 8-bit two-way one.
    wire [ROM_ADDR_WIDTH-1:0] branch_ofs = {is_jz ? {8{imm8[7]}} : {instr[15:12], instr[7:4]}, imm8};
    wire        jz_taken   = is_jz && (ra_val == 8'd0);

    // In DECEXEC the delta is always +1: the two "add zero" cases (an interrupt
    // entry, and a not-taken jz) are handled by simply not writing pc at all,
    // rather than by feeding a zero through the adder. What is left is a mux
    // against a constant, which collapses into a row of gates.
    wire [ROM_ADDR_WIDTH-1:0] delta_pc = (state == `S_DECEXEC) ? 'd1 : branch_ofs;
    wire [ROM_ADDR_WIDTH-1:0] pc_next  = pc + delta_pc;
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            pc <= 16'd0;
        end else begin
            case (state)
                `S_FETCH: begin
                    instr <= cur;
                end
                `S_DECEXEC: begin
                    if (!servicing_irq) pc <= pc_next;
                end
                `S_MA: begin
                    if (!servicing_irq && (is_ret_insn)) begin
                        pc[7:0] <= data_in;
                    end
                end
                `S_WB: begin
                    if (servicing_irq) begin
                        pc <= irq_vector;
                    end else if (jz_taken || is_jr || is_calr) begin
                        pc <= pc_next;
                    end else if (is_ret_insn) begin
                        pc[ROM_ADDR_WIDTH - 1:8] <= data_in;
                    end
`ifdef SIM
                    if (!servicing_irq && instr == 18'h3FFFF) $finish;
`endif
                end
            endcase
        end
    end

    // SP and AGU
    logic        cpu_mmio_hit;
    logic [7:0]  cpu_mmio_data;
    // Registered alongside cpu_mmio_hit, from the same cycle's address. Using
    // the combinational address[1:0] here instead put the shared adder's output
    // directly into the writeback cone (worth ~15 LUTs), and it also disagreed
    // with cpu_mmio_hit for `pop`, where SP -- and therefore address -- advances
    // between S_MA and S_WB, so hit was computed from one address and the byte
    // select from the next.
    logic [1:0]  cpu_mmio_sel;
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            sp <= 16'd0;
            servicing_irq <= 1'b0;
            irq_sel <= 3'd0;
        end else begin
            case (state)
                `S_FETCH: begin
                    servicing_irq <= take_irq;
                    irq_sel <= irq_sel_next;
                end
                `S_DECEXEC: begin
                    // is_decs (op6[5:2]==0010) is outside is_stack_grp (0011),
                    // so it does not get a second update in S_MA below.
                    if (servicing_irq || is_calr || is_ret_insn || is_decs) begin
                        sp <= add_out;
                    end
                end
                `S_MA: begin
                    if (servicing_irq || is_stack_grp || is_calr) begin
                        sp <= add_out;
                    end
                end
                `S_WB: begin
                    if (we && cpu_mmio_hit && (address[1:0] == 2'b00))
                        sp[7:0] <= data_out;
                    else if (we && cpu_mmio_hit && (address[1:0] == 2'b01))
                        sp[15:8] <= data_out;
                end
                default: ;
            endcase
            cpu_mmio_hit <= (address[15:2] == 14'h0000) ? 1'b1 : 1'b0;
            cpu_mmio_sel <= address[1:0];
        end
    end

    // Absolute mode bypasses the adder entirely. Everything that is not a memory
    // access (stack group, calr, and the classes that never drive `address` at
    // all) falls through to sp -- it is already a mux input, so the default arm
    // is free, and it keeps cpu_mmio_hit out of X during ALU/decs instructions.
    assign address =    servicing_irq ? sp             :
                        is_addr_n     ? {6'd0, imm10}  :
                        is_mem_grp    ? add_out        : sp;

    always_ff @( posedge clk or negedge reset_n ) begin
        if (!reset_n) begin
            data_out <= 8'hxx;
        end else begin
            if (state == `S_DECEXEC) begin
                data_out <= (servicing_irq || is_calr) ? pc[ROM_ADDR_WIDTH - 1:8] : 8'hxx;
            end else if (state == `S_MA) begin
                data_out <= (servicing_irq) ? pc[7:0] :
                            (is_push) ? ra_val :
                            (is_stm)  ? ra_val :
                            (is_calr) ? pc[7:0] : 8'hxx;
            end else begin
                data_out <= 8'hxx;
            end
        end
    end

    assign cpu_mmio_data =  (cpu_mmio_sel == 2'b00) ? sp[7:0]    :
                            (cpu_mmio_sel == 2'b01) ? sp[15:8]   :
                            (cpu_mmio_sel == 2'b10) ? psr        :
                            (cpu_mmio_sel == 2'b11) ? psr_shadow : 8'hxx;
    wire reg_we = is_alu || is_mvi || is_pop || is_ldm;

    wire [7:0] rw_next =    (is_pop || is_ldm) ?
                                (cpu_mmio_hit ? cpu_mmio_data : data_in)
                                : alu_out;
    // Register file
    //
    // regs_hi mirrors the odd registers (regs_hi[i] == r(2i+1)) so that a
    // register-pair base can be read in one cycle without a third read port on
    // regs: the rp field is instr[3:0] == {ppp,1'b0}, so the low half is already
    // on the rs read port (rb_val) and only the high half needs regs_hi[rs[3:1]].
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            // Nothing
        end else begin
            case (state)
                `S_WB: begin
                    if (!servicing_irq && reg_we) begin
                        regs[rd] <= rw_next;
                        if (rd[0]) regs_hi[rd[3:1]] <= rw_next;
                    end
                end
            endcase
        end
    end

    // PSR / psr_shadow: all writes consolidated into this one always_ff to avoid
    // multiple drivers (ALU writeback, software MMIO store, hardware IE auto-clear
    // on interrupt entry, and RETI's shadow restore all target the same register).
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            psr <= 2'b00;
            psr_shadow <= 2'b00;
        end else begin
            case (state)
                `S_WB: begin
                    if (!servicing_irq) begin
                        if (is_reti) begin
                            psr <= psr_shadow;
                        end else if (we && cpu_mmio_hit && (address[1:0] == 2'b10)) begin
                            psr[1:0] <= data_out[1:0];
                        end else if (we && cpu_mmio_hit && (address[1:0] == 2'b11)) begin
                            psr_shadow[1:0] <= data_out[1:0];
                        end else if (is_alu) begin
                            psr[0] <= carry_flag_next;
                        end
                    end else begin
                        psr_shadow <= psr;  // one-level auto-save
                        psr <= 2'b00;
                    end
                end
                default: ;
            endcase
        end
    end

endmodule
