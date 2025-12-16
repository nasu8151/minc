module minc (
    input  logic        CLK,
    input  logic        nRESET,
    output logic [7:0]  pc_out,
    output logic [7:0]  top_out,
    output logic [7:0]  sp_out
);

    // PC, SP
    logic  [7:0] pc;
    logic  [7:0] sp;

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15];

    //flags
    logic zero_flag;
    logic carry_flag;

    // Instruction ROM: 256 words x 15-bit (instruction is 15-bit)
    logic  [14:0] rom  [0:255];
    // Data RAM: 256 x 8-bit (stack and data unified)
    logic  [7:0]  ram  [0:255];

    // ROM load (one word per line, hex). TEST selects test.hex
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    // Outputs
    assign pc_out  = pc;
    assign sp_out  = sp;
    assign top_out = ram[sp]; // current stack top

    // Fetch current instruction (15-bit in [14:0])
    wire [14:0] instr = rom[pc];

    // Decode fields
    wire [2:0] op    = instr[14:12];
    wire [3:0] subop = instr[11:8];
    wire [3:0] rd    = op[0] == 1'b1 ? instr[3:0] : instr[7:4];
    wire [3:0] rs    = instr[3:0];
    wire [7:0] imm8  = instr[11:4];
    integer i;

    // Next PC logic
    logic [7:0] next_pc;
    always_comb begin
        if (op == 3'b000) begin
            if (subop == 4'b1100) begin
                // ret instruction special case: next_pc from stack
                next_pc = ram[sp] + 8'd1;
            end else begin
                next_pc = pc + 8'd1;
            end
        end else if (op == 3'b100) begin
            // conditional jump taken
            case (rs[1:0])
                2'b00: begin
                    // jz
                    if (zero_flag) begin
                        next_pc = imm8;
                    end else begin
                        next_pc = pc + 8'd1;
                    end
                end
                2'b01: begin
                    // jc
                    if (carry_flag) begin
                        next_pc = imm8;
                    end else begin
                        next_pc = pc + 8'd1;
                    end
                end
                default: begin
                    next_pc = pc + 8'd1; // other conditions not implemented
                end
            endcase
        end else if (op == 3'b101) begin
            // call
            next_pc = imm8;
        end else begin
            next_pc = pc + 8'd1;
        end
    end

    always_ff @(posedge CLK or negedge nRESET) begin
        if (!nRESET) begin
            pc <= 8'h00;
            sp <= 8'h00;
            carry_flag <= 1'b0;
            zero_flag <= 1'b0;
            // Clear registers for deterministic startup
            for (i = 0; i < 16; i = i + 1) begin
                regs[i] <= 8'h00;
            end
        end else begin
            // Default next PC is sequential (one word per instruction)

            // Execute
            case (op)
                3'b000: begin
                    // subop-based operations
                    case (subop)
                        4'b0000: begin
                            // mov rd,rs : rd = rs
                            regs[rd] <= regs[rs];
                            zero_flag <= (regs[rs] == 8'd0) ? 1'b1 : 1'b0;
                            $display("mov r%0d, r%0d", rd, rs);
                        end
                        4'b0001: begin
                            // add rd,rs : rd = rd + rs
                            {carry_flag, regs[rd]} <= regs[rd] + regs[rs];
                            zero_flag <= (regs[rd] + regs[rs] == 8'd0) ? 1'b1 : 1'b0;
                            $display("add r%0d, r%0d", rd, rs);
                        end
                        4'b0010: begin
                            // sub rd,rs : rd = rd - rs
                            {carry_flag, regs[rd]} <= regs[rd] - regs[rs];
                            zero_flag <= (regs[rd] - regs[rs] == 8'd0) ? 1'b1 : 1'b0;
                            $display("sub r%0d, r%0d", rd, rs);
                        end
                        4'b0011: begin
                            // mul rd,rs : rd = rd * rs
                            regs[rd] <= regs[rd] * regs[rs];
                            zero_flag <= (regs[rd] * regs[rs] == 8'd0) ? 1'b1 : 1'b0;
                            $display("mul r%0d, r%0d", rd, rs);
                        end
                        4'b1000: begin
                            // push rs : (--sp) = rs  (pattern 000 1000 0000 ssss)
                            ram[sp - 8'd1] <= regs[rs];
                            sp <= sp - 8'd1; // wrap naturally (8-bit)
                            $display("push r%0d", rs);
                        end
                        4'b1001: begin
                            // lds rs : SP = rs (pattern 000 0100 0001 ssss)
                            sp <= regs[rs];
                        end
                        4'b1010: begin
                            // pop rd : rd = (SP++) (pattern 000 0101 dddd 0000)
                            $display("pop r%0d", rd);
                            regs[rd] <= ram[sp];
                            sp <= sp + 8'd1;
                        end
                        4'b1011: begin
                            // sts rd : rd = SP (pattern 000 0101 dddd 0001)
                            regs[rd] <= sp;
                        end
                        4'b1100: begin
                            // ret : PC = (SP++) + 1 (pattern 000 0101 0000 0010)
                            next_pc = ram[sp] + 8'd1;
                            sp <= sp + 8'd1;
                        end
                        default: begin
                            // no-op for undefined subops in this group
                        end
                    endcase
                end
                3'b001: begin
                    // mvi rd, n : rd = n
                    regs[rd] <= imm8;
                    $display("mvi r%0d, 0x%0h", rd, imm8);
                end
                3'b010: begin
                    // stm n, rs : [r15 + n] = rs
                    ram[regs[15] + imm8] <= regs[rs];
                end
                3'b011: begin
                    // ldm n, rd : rd = [r15 + n]
                    regs[rd] <= ram[regs[15] + imm8];
                end
                3'b100: begin
                    // jz n, rs : PC = n if rs == 0
                end
                3'b101: begin
                    // call n : (--sp) = PC; PC = n  (low nibble must be 0000)
                    ram[sp - 8'd1] <= pc;
                    sp <= sp - 8'd1;
                end
                default: begin
                    // 110,111: unused -> HALT
                end
            endcase

            // Commit next PC
            pc <= next_pc;
        end
    end

endmodule