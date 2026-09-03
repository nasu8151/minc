`define S_FETCH   2'b00
`define S_DECEXEC 2'b01
`define S_MA      2'b10
`define S_WB      2'b11

// minc-16: 16-bit datapath, byte-addressed data space, 18-bit instruction word.
// See Hardware.md "### minc-16" for the encoding and "## minc-16 設計メモ" for the rationale.
//
// Deliberately NOT named `minc`: the port list (16-bit data bus, 2-bit byte-lane
// `we`) is incompatible with minc_h.sv/minc_p2.sv/minc_p5.sv, so it must not be
// drop-in swapped into minc_tb.sv by accident. Use minc16_tb.sv.
module minc16 (
    input  logic        clk,
    input  logic        reset_n,
    output logic [15:0] pc_out,
    output logic [15:0] sp_out,
    output logic [15:0] address,   // byte address
    output logic [15:0] data_out,
    output logic [1:0]  we,        // byte lane write enables: we[0]=even byte, we[1]=odd byte
    output logic        avma,
    input  logic [15:0] data_in,
    input  logic        wait_req,
    input  logic [3:0]  irq_in
);

    logic [15:0] pc;

    logic [1:0] state;
    logic servicing_irq; // 1 for the 4-state hardware interrupt-entry pseudo-op (push + jump to IRQ_VECTOR)
    logic [2:0] irq_sel; // which irq_in line was accepted (0 = highest priority), latched on entry

    // General purpose registers r0..r15 (16-bit).
    // r15 = SP (hardware-enforced: push/pop/calr/ret/reti/IRQ entry update it).
    // r14 = BP, r13 = X are software convention only -- the hardware does not know them.
    // No reg12..reg15 shadow copies are needed here: a pointer fits in one register,
    // so the address base comes through the normal port-A read.
    logic [15:0] regs [0:15];

    // Instruction ROM: sized to fit 4 pROMX9 blocks (4096 x 18-bit).
    // Instruction space stays word-indexed (1 word = 1 instruction); only the
    // *data* space is byte-addressed.
    localparam ROM_ADDR_WIDTH = 12;
    logic [17:0] rom [0:(1<<ROM_ADDR_WIDTH)-1]; /* synthesis syn_romstyle = "BLOCK_ROM" */
    wire [17:0] cur = rom[pc[ROM_ADDR_WIDTH-1:0]];

    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    assign pc_out = pc;
    assign sp_out = regs[15];

    logic [17:0] instr;

    // PSR: bit0 = carry, bit1 = ie. Memory-mapped as byte 0x0002; PSR_SHADOW as
    // byte 0x0003 -- i.e. the low and high byte of the same 16-bit word, so
    // `ldw rd,2` fetches both at once and `ldb rd,2` / `stb 2,rs` touch just PSR.
    logic [1:0] psr;
    logic [1:0] psr_shadow;
    wire        carry_flag = psr[0];
    wire        ie         = psr[1];
    logic       carry_next;

    /* ------------------------------------------------------------------ *
     * Decode
     * ------------------------------------------------------------------ */
    wire [1:0] grp = instr[17:16];
    wire [1:0] sub = instr[15:14];

    wire is_g0   = (grp == 2'b00);
    wire is_alu  = is_g0 && (sub == 2'b00);
    wire is_imm  = is_g0 && (sub == 2'b01);
    wire is_abs  = is_g0 && (sub == 2'b10);
    wire is_ctl  = is_g0 && (sub == 2'b11);
    wire is_disp = (grp == 2'b01);
    wire is_calr = (grp == 2'b10);
    wire is_jr   = (grp == 2'b11);

    wire [3:0] alu_subop = instr[13:10];
    wire [1:0] imm_subop = instr[13:12];
    wire [1:0] ctl_subop = instr[13:12];
    wire [3:0] ctl_ext   = instr[11:8];

    // Memory: abs/disp differ only in where the ld/st and byte/word bits sit.
    wire mem_is_ld = is_abs ? instr[13] : instr[15];
    wire mem_is_b  = is_abs ? instr[12] : instr[14];
    wire is_mem    = is_abs || is_disp;
    wire is_load   = is_mem && mem_is_ld;
    wire is_store  = is_mem && !mem_is_ld;

    wire is_jz   = is_ctl && (ctl_subop == 2'b00);
    wire is_jnz  = is_ctl && (ctl_subop == 2'b01);
    wire is_stk  = is_ctl && (ctl_subop == 2'b10);
    wire is_push = is_stk && (ctl_ext == 4'b0000);
    wire is_pop  = is_stk && (ctl_ext == 4'b0001);
    wire is_ret  = is_stk && (ctl_ext == 4'b0010);
    wire is_reti = is_stk && (ctl_ext == 4'b0011);

    // Immediates. imm8 is split as {[11:8],[3:0]} so that the immediate group's
    // destination register can live at [7:4] like the ALU group's rd -- that is
    // what keeps `addi` off a second ALU A-input mux (Hardware.md 設計メモ).
    wire [7:0]  imm8      = {instr[11:8], instr[3:0]};
    wire [15:0] simm8     = {{8{imm8[7]}}, imm8};
    wire [7:0]  abs8      = instr[11:4];
    wire [7:0]  ctl_imm8  = instr[11:4];
    wire [15:0] ctl_simm8 = {{8{ctl_imm8[7]}}, ctl_imm8};
    wire [5:0]  disp6     = instr[13:8];
    wire [15:0] simm6     = {{10{disp6[5]}}, disp6};
    wire [15:0] simm16    = instr[15:0];

    /* ------------------------------------------------------------------ *
     * Register file read ports
     * ------------------------------------------------------------------ */
    // Stack ops steer port A at r15 so SP arithmetic reuses the same ALU path
    // as address calculation. Everything that needs to be an ALU *A* operand
    // (ALU rd, addi rd, memory base) is encoded at [7:4] by construction.
    wire is_sp_op = is_push || is_pop || is_ret || is_reti || is_calr || servicing_irq;
    wire [3:0] pa_idx = is_sp_op ? 4'd15 : instr[7:4];
    wire [3:0] pb_idx = instr[3:0];
    wire [15:0] pa_val = regs[pa_idx];
    wire [15:0] pb_val = regs[pb_idx];

    /* ------------------------------------------------------------------ *
     * ALU -- shared by data ops, address calculation and SP +/- 2
     * ------------------------------------------------------------------ */
    wire is_addi   = is_imm && (imm_subop == 2'b10);
    wire is_sp_dec = is_push || is_calr || servicing_irq;
    wire is_sp_inc = is_pop || is_ret || is_reti;

    // `servicing_irq` must outrank every decode of `instr` below. The interrupt
    // entry pseudo-op runs with `instr` already holding the *deferred* instruction
    // (S_FETCH latches `instr <= cur` and raises `servicing_irq` on the same edge),
    // so without this priority the deferred instruction's opcode would steer the
    // ALU that the entry sequence needs for SP -= 2. minc-8 was immune to this
    // because SP had its own adder; sharing the ALU is what introduces the hazard.
    //
    // Non-ALU-group instructions force ADD.
    wire [3:0] alu_op = (is_alu && !servicing_irq) ? alu_subop : 4'b0100;

    wire [15:0] alu_a = pa_val;
    wire [15:0] alu_b = servicing_irq ? 16'hFFFE :
                        is_disp   ? simm6   :
                        is_addi   ? simm8   :
                        is_sp_dec ? 16'hFFFE : // -2
                        is_sp_inc ? 16'd2    :
                                    pb_val;

    // sub/sbc (0110/0111) and lt/ltc (1010/1011) all have bit1 set; the other
    // subops with bit1 set (and/xor/mul/mulh) never read the adder result.
    wire        alu_do_sub = alu_op[1];
    wire        alu_use_cf = alu_op[0];
    wire [15:0] alu_b_eff  = alu_do_sub ? ~alu_b : alu_b;
    wire        alu_cin    = alu_use_cf ? carry_flag : alu_do_sub;
    wire        alu_cout;
    wire [15:0] alu_sum;
    assign {alu_cout, alu_sum} = alu_a + alu_b_eff + alu_cin;

    wire [31:0] mul_res = alu_a * alu_b;

    logic [15:0] alu_out;
    always_comb begin
        case (alu_op)
            4'b0000: begin alu_out = alu_b;             carry_next = 1'bx; end // MOV
            4'b0001: begin alu_out = alu_a | alu_b;     carry_next = 1'bx; end // OR
            4'b0010: begin alu_out = alu_a & alu_b;     carry_next = 1'bx; end // AND
            4'b0011: begin alu_out = alu_a ^ alu_b;     carry_next = 1'bx; end // XOR
            4'b0100, 4'b0101, 4'b0110, 4'b0111: begin                          // ADD/ADC/SUB/SBC
                alu_out = alu_sum; carry_next = alu_cout;
            end
            4'b1000: begin alu_out = {15'b0, ~|alu_a};  carry_next = 1'bx; end // CHZ
            4'b1001: begin alu_out = {{8{alu_b[7]}}, alu_b[7:0]}; carry_next = 1'bx; end // SXB
            4'b1010, 4'b1011: begin alu_out = {15'b0, ~alu_cout}; carry_next = 1'bx; end // LT/LTC
            4'b1100: begin alu_out = {carry_flag, alu_b[15:1]};   carry_next = alu_b[0]; end // RR
            4'b1101: begin alu_out = {alu_b[15], alu_b[15:1]};    carry_next = alu_b[0]; end // ASR
            4'b1110: begin alu_out = mul_res[15:0];     carry_next = 1'bx; end // MUL
            4'b1111: begin alu_out = mul_res[31:16];    carry_next = 1'bx; end // MULH
            default: begin alu_out = alu_a;             carry_next = 1'bx; end
        endcase
    end

    /* ------------------------------------------------------------------ *
     * State machine
     * ------------------------------------------------------------------ */
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state <= `S_FETCH;
        end else begin
            case (state)
                `S_FETCH:   state <= `S_DECEXEC;
                `S_DECEXEC: state <= `S_MA;
                `S_MA:      state <= `S_WB;
                `S_WB: begin
                    if (!servicing_irq && is_mem && wait_req) state <= `S_WB;
                    else state <= `S_FETCH;
                end
                default: state <= `S_FETCH;
            endcase
        end
    end

    wire do_store = is_store || is_push || is_calr || servicing_irq;
    // The byte-lane narrowing is gated on !servicing_irq for the same reason: the
    // interrupt push is always a full word, even if the deferred instruction is stb.
    assign we = ((state == `S_WB) && do_store)
                ? ((!servicing_irq && is_store && mem_is_b) ? (address[0] ? 2'b10 : 2'b01) : 2'b11)
                : 2'b00;
    assign avma = !servicing_irq && is_mem && (state == `S_MA || state == `S_WB);

    /* ------------------------------------------------------------------ *
     * Interrupts
     * ------------------------------------------------------------------ */
    wire any_irq = |irq_in;
    wire [2:0] irq_sel_next = irq_in[0] ? 3'd1 :
                              irq_in[1] ? 3'd2 :
                              irq_in[2] ? 3'd3 :
                              irq_in[3] ? 3'd4 : 3'dx;
    wire take_irq = ie && any_irq;
    wire [15:0] irq_vector = {13'd0, irq_sel};

    /* ------------------------------------------------------------------ *
     * PC
     * ------------------------------------------------------------------ */
    wire branch_taken = (is_jz  && (pb_val == 16'd0)) ||
                        (is_jnz && (pb_val != 16'd0));
    wire [15:0] delta_pc = (state == `S_DECEXEC) ? (servicing_irq ? 16'd0 : 16'd1) :
                           (is_jz || is_jnz)     ? (branch_taken ? ctl_simm8 : 16'd0) :
                           (is_jr || is_calr)    ? simm16 : 16'hxxxx;
    wire [15:0] pc_next = pc + delta_pc;

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            pc <= 16'd0;
        end else begin
            case (state)
                `S_FETCH: begin
                    instr <= cur;
                end
                `S_DECEXEC: begin
                    pc <= pc_next;
                end
                `S_WB: begin
                    if (servicing_irq) begin
                        pc <= irq_vector;
                    end else if (is_jz || is_jnz || is_jr || is_calr) begin
                        pc <= pc_next;
                    end else if (is_ret || is_reti) begin
                        // 16-bit PC arrives in a single word -- minc-8 needed two
                        // byte transfers across S_MA and S_WB for this.
                        pc <= data_in;
                    end
`ifdef SIM
                    if (!servicing_irq && instr == 18'h3FFFF) $finish;
`endif
                end
                default: ;
            endcase
        end
    end

    /* ------------------------------------------------------------------ *
     * Address generation
     * ------------------------------------------------------------------ */
    // Stack ops read the address straight off port A (= r15). r15 is updated at
    // the end of S_MA, which lines both directions up correctly:
    //   push/calr/irq -- S_WB sees the already-decremented SP, which is where the
    //                    word must land.
    //   pop/ret       -- S_MA still sees the old SP, which is where the word is
    //                    read from; the memory captures it on the S_MA->S_WB edge.
    // servicing_irq first: see the alu_op/alu_b note above -- a deferred abs/disp
    // instruction would otherwise drive the address during interrupt entry.
    assign address = servicing_irq ? pa_val :
                     is_abs  ? {8'd0, abs8} :
                     is_disp ? alu_out :
                     is_sp_op ? pa_val : 16'hxxxx;

    /* ------------------------------------------------------------------ *
     * Store data
     * ------------------------------------------------------------------ */
    wire [15:0] st_data = (is_store && mem_is_b) ? {pb_val[7:0], pb_val[7:0]} : pb_val;

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            data_out <= 16'hxxxx;
        end else if (state == `S_MA) begin
            // pc has already been incremented in S_DECEXEC, so this is the
            // return address (PC+1) for calr. For an interrupt, delta_pc was 0,
            // so pc is still the instruction to resume at.
            data_out <= (servicing_irq || is_calr) ? pc :
                        (is_push || is_store)      ? st_data : 16'hxxxx;
        end else begin
            data_out <= 16'hxxxx;
        end
    end

    /* ------------------------------------------------------------------ *
     * Load data / register writeback
     * ------------------------------------------------------------------ */
    // PSR (byte 0x0002) and PSR_SHADOW (byte 0x0003) are the two halves of word 1.
    wire psr_sel = is_mem && (address[15:1] == 15'd1);
    wire [15:0] psr_word = {6'd0, psr_shadow, 6'd0, psr};

    wire [15:0] mem_word  = psr_sel ? psr_word : data_in;
    wire [7:0]  mem_byte  = address[0] ? mem_word[15:8] : mem_word[7:0];
    // The byte narrowing is gated on is_load so that `pop` can share this path:
    // pop is a ctl-group instruction, so is_mem/psr_sel are 0 and mem_word falls
    // through to data_in -- exactly what pop needs -- but mem_is_b would otherwise
    // be a stray decode of instr[15] and could truncate the popped word.
    wire [15:0] mem_rdata = (is_load && mem_is_b) ? {8'd0, mem_byte} : mem_word;

    wire [15:0] imm_out = (imm_subop == 2'b00) ? simm8 :                    // mvi
                          (imm_subop == 2'b01) ? {imm8, pa_val[7:0]} :      // mvih
                                                 alu_out;                   // addi

    // 3-way, not 4-way: pop rides the load path (see mem_rdata above). Folding the
    // two memory sources together is worth ~50 LUT on GW1N -- this mux is 16 bits
    // wide and sits on top of the already-deep alu_out cone.
    wire [15:0] rw_next = is_alu  ? alu_out :
                          is_imm  ? imm_out : mem_rdata;

    // Only the ALU and immediate groups write [7:4]; loads and pop write [3:0].
    wire [3:0] wb_idx = (is_alu || is_imm) ? instr[7:4] : instr[3:0];
    wire       wb_en  = is_alu || is_imm || is_load || is_pop;

    // The two writers -- the SP update in S_MA and the destination writeback in
    // S_WB -- are flattened into ONE unconditional write port with no reset, and
    // that shape is load-bearing: it is what lets GowinSynthesis infer 8 RAM16SDP4
    // (distributed RAM) for `regs`. Split into two `case (state)` arms, or given an
    // async reset on regs[15], the array falls back to 256 flip-flops plus two
    // 16:1x16-bit LUT mux trees for pa_val/pb_val -- 256 of the core's ~714 LUT.
    // Keep it a single `if (rf_we) regs[rf_widx] <= rf_wdat;` when touching this.
    //
    // The two enables are mutually exclusive by state, so rf_we_ma wins the muxes
    // for free. SP therefore has no hardware reset value any more: the program must
    // set r15 itself before the first push (every fixture under tests/fixtures/m16
    // already does).
    wire        rf_we_ma = (state == `S_MA) && is_sp_op;
    wire        rf_we_wb = (state == `S_WB) && !servicing_irq && wb_en;
    wire        rf_we    = rf_we_ma || rf_we_wb;
    wire [3:0]  rf_widx  = rf_we_ma ? 4'd15 : wb_idx;
    wire [15:0] rf_wdat  = rf_we_ma ? alu_out : rw_next;

    always_ff @(posedge clk) begin
        if (rf_we) regs[rf_widx] <= rf_wdat;
    end

    /* ------------------------------------------------------------------ *
     * PSR / PSR_SHADOW
     * ------------------------------------------------------------------ */
    // All writers consolidated here to avoid multiple drivers: ALU carry, addi
    // carry, software MMIO store, hardware IE auto-clear, and RETI's restore.
    wire psr_store_lo = (we[0] && address[15:1] == 15'd1);
    wire psr_store_hi = (we[1] && address[15:1] == 15'd1);

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            psr <= 2'b00;
            psr_shadow <= 2'b00;
        end else if (state == `S_WB) begin
            if (!servicing_irq) begin
                if (is_reti) begin
                    psr <= psr_shadow;
                end else if (psr_store_lo || psr_store_hi) begin
                    if (psr_store_lo) psr        <= data_out[1:0];
                    if (psr_store_hi) psr_shadow <= data_out[9:8];
                end else if (is_alu || is_addi) begin
                    psr[0] <= carry_next;
                end
            end else begin
                psr_shadow <= psr;  // one-level auto-save
                psr <= 2'b00;
            end
        end
    end

    /* ------------------------------------------------------------------ *
     * IRQ entry latch
     * ------------------------------------------------------------------ */
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            servicing_irq <= 1'b0;
            irq_sel <= 3'd0;
        end else if (state == `S_FETCH) begin
            servicing_irq <= take_irq;
            irq_sel <= irq_sel_next;
        end
    end

endmodule
