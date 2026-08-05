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
    logic [15:0] pc;
    logic [15:0] sp;

    logic [1:0] state;
    logic servicing_irq; // 1 for the 4-state hardware interrupt-entry pseudo-op (push + jump to IRQ_VECTOR)
    logic [2:0] irq_sel; // which irq_in line was accepted (0 = highest priority), latched on entry

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15];
    logic  [7:0]  reg12;
    logic  [7:0]  reg13;
    logic  [7:0]  reg14;
    logic  [7:0]  reg15;


    // Instruction ROM: sized to fit 4 pROMX9 blocks (4096 x 18-bit).
    // pc stays 16-bit for pc_out/branch arithmetic, but only its low 12
    // bits address the ROM so the synthesizer doesn't need to prove pc's
    // reachable range to keep this out of LUT-based fallback.
    localparam ROM_ADDR_WIDTH = 12;
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

    wire [7:0] imm8 = {instr[11:8], instr[3:0]};
    wire [15:0] imm16 = {instr[15:12], instr[7:4], instr[11:8], instr[3:0]};
    wire signed [15:0] simm8  = 16'($signed(imm8));
    wire signed [15:0] simm16 = 16'($signed(imm16));

    // wire [7:0] rd_val = regs[rd];
    // wire [7:0] rs_val = regs[rs];

    wire [7:0] ra_val = regs[rd];
    wire [7:0] rb_val = regs[rs];

    logic [7:0] alu_out;

    wire is_alu = (op4 == 4'b0000);
    // wire is_mem_like = (op2 != 2'b00);

    // wire is_mul    = (op6 == 6'b000100);
    // wire is_mulh   = (op6 == 6'b000101);
    // wire is_stf    = (op6 == 6'b001000);
    // wire is_clf    = (op6 == 6'b001001);
    wire is_jz     = (op6 == 6'b001100);
    wire is_mvi    = (op6 == 6'b001110);

    // stm/ldm share op6[5:3]==010; op6[2:1] picks the addressing mode
    // (X/Y/N) and op6[0] picks store vs. load, so decode each field once
    // instead of six separate 6-bit comparators.
    wire is_mem_grp = (op6[5:3] == 3'b010);
    wire is_stm     = is_mem_grp && (op6[0] == 1'b0);
    wire is_ldm     = is_mem_grp && (op6[0] == 1'b1);
    wire is_addr_x  = is_mem_grp && (op6[2:1] == 2'b00);
    wire is_addr_y  = is_mem_grp && (op6[2:1] == 2'b01);
    wire is_addr_n  = is_mem_grp && (op6[2:1] == 2'b10);
    wire is_push   = (op6 == 6'b011100);
    wire is_pop    = (op6 == 6'b011101);
    wire is_ret    = (op6 == 6'b011111);
    wire is_reti   = (op6 == 6'b011110);
    wire is_calr   = (op2 == 2'b10);
    wire is_jr     = (op2 == 2'b11);

    // ALU
    // ADD/ADC/SUB/SBC/LT share one 8-bit adder: subop[3]|subop[1] selects
    // subtract (invert b, cin defaults to 1), subop[0] selects carry-in from
    // the flag (ADC/SBC). LT reads out the borrow instead of the sum.
    wire       alu_do_sub = subop[1];
    wire       alu_use_cf = subop[0];
    wire [7:0] alu_b      = alu_do_sub ? ~rb_val : rb_val;
    wire       alu_cin    = alu_use_cf ? carry_flag : alu_do_sub;
    wire       alu_cout;
    wire [7:0] alu_sum;
    assign {alu_cout, alu_sum} = ra_val + alu_b + alu_cin;

    always_comb begin
        case (subop)
            4'b0000: begin alu_out = rb_val; carry_flag_next = 1'bx; end // MOV
            4'b0001: begin alu_out = ra_val | rb_val; carry_flag_next = 1'bx; end // OR
            4'b0010: begin alu_out = ra_val & rb_val; carry_flag_next = 1'bx; end // AND
            4'b0011: begin alu_out = ra_val ^ rb_val; carry_flag_next = 1'bx; end // XOR
            4'b0100, 4'b0101, 4'b0110, 4'b0111: begin // ADD, ADC, SUB, SBC
                alu_out = alu_sum;
                carry_flag_next = alu_cout;
            end
            4'b1000: begin alu_out = {7'b0, ~|ra_val};  carry_flag_next = 1'bx; end // CHZ
            4'b1010, 4'b1011: begin alu_out = {7'b0, ~alu_cout}; carry_flag_next = 1'bx; end // LT and LTC (shares the subtractor above)
            4'b1100: {alu_out, carry_flag_next} = {carry_flag, rb_val >> 1}; // ROR
            4'b1110: begin alu_out = ra_val * rb_val; carry_flag_next = 1'bx; end // MUL
            4'b1111: begin alu_out = (ra_val * rb_val) >> 8; carry_flag_next = 1'bx; end // MULH
            default: begin alu_out = ra_val; carry_flag_next = 1'bx; end
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
    // MMIO addresses used by PSR/psr_shadow below, despite the similar-looking numbers.
    wire any_irq = |irq_in;
    wire [2:0] irq_sel_next =   irq_in[0] ? 3'd1 :
                                irq_in[1] ? 3'd2 :
                                irq_in[2] ? 3'd3 :
                                irq_in[3] ? 3'd4 : 3'dx;
    wire take_irq = ie && any_irq;
    wire [15:0] irq_vector = {13'd0, irq_sel};

    // PC and ROM control
    wire [15:0] delta_pc =  (state == `S_DECEXEC) ? (servicing_irq ? 16'd0 : 16'd1) :
                            (is_jz) ? (ra_val == 8'd0) ? simm8 : 16'd0 :
                            (is_jr || is_calr) ? simm16 : 16'hxxxx;
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
                `S_MA: begin
                    if (!servicing_irq && (is_ret || is_reti)) begin
                        pc[7:0] <= data_in;
                    end
                end
                `S_WB: begin
                    if (servicing_irq) begin
                        pc <= irq_vector;
                    end else if (is_jz || is_jr || is_calr) begin
                        pc <= pc_next;
                    end else if (is_ret || is_reti) begin
                        pc[15:8] <= data_in;
                    end
`ifdef SIM
                    if (!servicing_irq && instr == 18'h3FFFF) $finish;
`endif
                end
            endcase
        end
    end

    // SP and AGU
    logic [15:0] addr_base;
    logic aeq0;
    logic aeq1;
    logic aeq2;
    logic aeq3;
    wire [15:0] delta_sp = servicing_irq ? -16'd1 :
                        (is_calr || is_push) ? -16'd1 :
                        (is_pop || is_ret || is_reti) ? 16'd1 : 16'hxxxx;
    wire [15:0] sp_next = sp + delta_sp;
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
                    if (servicing_irq || is_calr || is_ret || is_reti) begin
                        sp <= sp_next;
                    end
                end
                `S_MA: begin
                    if (servicing_irq || is_push || is_calr || is_pop || is_ret || is_reti) begin
                        sp <= sp_next;
                    end
                end
                `S_WB: begin
                    if (we && (address == 16'h0000))
                        sp[7:0] <= data_out;
                    else if (we && (address == 16'h0001))
                        sp[15:8] <= data_out;
                end
                default: ;
            endcase
            aeq0 <= (address == 16'h0000) ? 1'b1 : 1'b0;
            aeq1 <= (address == 16'h0001) ? 1'b1 : 1'b0;
            aeq2 <= (address == 16'h0002) ? 1'b1 : 1'b0;
            aeq3 <= (address == 16'h0003) ? 1'b1 : 1'b0;
        end
    end
    assign addr_base =  (is_addr_x) ? {reg13, reg12} :
                        (is_addr_y) ? {reg15, reg14} :
                        (is_addr_n) ? 16'h0000 : 16'hxxxx;
    assign address =    servicing_irq ? sp :
                        (is_mem_grp) ? (addr_base + (is_addr_n ? {8'h00, imm8} : simm8)) :
                        (is_calr || is_push || is_pop || is_ret || is_reti) ? sp : 16'hxxxx;

    always_ff @( posedge clk or negedge reset_n ) begin
        if (!reset_n) begin
            data_out <= 8'hxx;
        end else begin
            if (state == `S_DECEXEC) begin
                data_out <= (servicing_irq || is_calr) ? pc[15:8] : 8'hxx;
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

    wire [7:0] rw_next =    (is_alu) ? alu_out : 
                            (is_mvi) ? imm8 : 
                            (is_pop || is_ldm) ? aeq0 ? sp[7:0] :
                                aeq1 ? sp[15:8] :
                                aeq2 ? psr :
                                aeq3 ? psr_shadow : data_in : ra_val;
    // Register file
    
    // is_ldm_x||is_stm_x reduces to is_addr_x (is_ldm|is_stm == is_mem_grp,
    // which is_addr_x already implies); the DECEXEC-only gate for stores
    // is factored out once as agu_rd_active.

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            // Nothing
        end else begin
            case (state)
                `S_WB: begin
                    if (!servicing_irq) begin
                        regs[rd] <= rw_next;
                        case (rd)
                            4'd12: reg12 <= rw_next;
                            4'd13: reg13 <= rw_next;
                            4'd14: reg14 <= rw_next;
                            4'd15: reg15 <= rw_next;
                        endcase
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
                        end else if (we && address == 16'h0002) begin
                            psr[1:0] <= data_out[1:0];
                        end else if (we && address == 16'h0003) begin
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
