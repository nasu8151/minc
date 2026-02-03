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

    logic [1:0] state;

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15];

    // Instruction ROM: 256 words x 15-bit (instruction is 15-bit)
    logic  [15:0] rom  [0:255];
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
    wire [14:0] instr = rom[pc][14:0];

    // Decode fields
    wire [2:0] op    = instr[14:12];
    wire [3:0] subop = instr[11:8];
    wire [3:0] rd    = op[0] == 1'b1 ? instr[3:0] : instr[7:4];
    wire [3:0] rs    = instr[3:0];
    wire [7:0] imm8  = instr[11:4];

    wire [7:0] rd_val = regs[rd];
    wire [7:0] rs_val = regs[rs];
    integer i;

    // Next PC logic
    wire [7:0] next_pc =    op == 3'b000 && subop == 4'b1100 ? ram[sp] + 8'd1 :
                            op == 3'b100 ? (regs[rs] == 8'd0 ? imm8 : pc + 8'd1) :
                            op == 3'b101 ? imm8 :
                            op == 3'b110 ? (regs[rs] != 8'd0 ? imm8 : pc + 8'd1) :
                            pc + 8'd1;

    // Main sequential logic
    always_ff @(posedge CLK or negedge nRESET) begin
        if (!nRESET) begin
            pc <= 8'h00;
            sp <= 8'h00;
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
                    casez (subop)
                        4'b0???: begin
                            // ALU operations: rd = rd <op> rs
                            regs[rd] <= alu_out;
                            `ifdef SIM
                            case (subop)
                                4'b0000: $display("mov r%0d, r%0d", rd, rs);
                                4'b0001: $display("add r%0d, r%0d", rd, rs);
                                4'b0010: $display("sub r%0d, r%0d", rd, rs);
                                4'b0011: $display("cmp r%0d, r%0d", rd, rs);
                                4'b0100: $display("mul r%0d, r%0d", rd, rs);
                                default: $display("Unknown ALU operation: 0x%0h", instr); // should not occur
                            endcase
                            `endif
                        end
                        4'b1000: begin
                            // push rs : (--sp) = rs  (pattern 000 1000 0000 ssss)
                            ram[ram_addr] <= regs[rs];
                            sp <= ram_addr; // wrap naturally (8-bit)
                            `ifdef SIM
                            $display("push r%0d", rs);
                            `endif
                        end
                        4'b1001: begin
                            // lds rs : SP = rs (pattern 000 0100 0001 ssss)
                            sp <= regs[rs];
                            `ifdef SIM
                            $display("lds r%0d", rs);
                            `endif
                        end
                        4'b1010: begin
                            // pop rd : rd = (SP++) (pattern 000 0101 dddd 0000)
                            regs[rd] <= ram[sp];
                            sp <= sp + 8'd1;
                            `ifdef SIM
                            $display("pop r%0d", rd);
                            `endif
                        end
                        4'b1011: begin
                            // sts rd : rd = SP (pattern 000 0101 dddd 0001)
                            regs[rd] <= sp;
                            `ifdef SIM
                            $display("sts r%0d", rd);
                            `endif
                        end
                        4'b1100: begin
                            // ret : PC = (SP++) + 1 (pattern 000 0101 0000 0010)
                            ram_addr = sp;
                            // next_pc = ram[ram_addr] + 8'd1;
                            sp <= sp + 8'd1;
                            `ifdef SIM
                            $display("ret");
                            `endif
                        end
                        default: begin
                            // no-op for undefined subops in this group
                        end
                    endcase
                end
                3'b001: begin
                    // mvi rd, n : rd = n
                    regs[rd] <= imm8;
                    `ifdef SIM
                    $display("mvi r%0d, 0x%0h", rd, imm8);
                    `endif
                end
                3'b010: begin
                    // stm n, rs : [r15 + n] = rs
                    ram[ram_addr] <= regs[rs];
                    `ifdef SIM
                    $display("stm 0x%0h, r%0d", imm8, rs);
                    `endif
                end
                3'b011: begin
                    // ldm n, rd : rd = [r15 + n]
                    regs[rd] <= ram[ram_addr];
                    `ifdef SIM
                    $display("ldm 0x%0h, r%0d", imm8, rd);
                    `endif
                end
                3'b100: begin
                    // jz n, rs : PC = n if rs == 0
                    `ifdef SIM
                    $display("jz 0x%0h, r%0d", imm8, rs);
                    `endif
                end
                3'b101: begin
                    // call n : (--sp) = PC; PC = n  (low nibble must be 0000)
                    ram[ram_addr] <= pc;
                    sp <= sp - 8'd1;
                    `ifdef SIM
                    $display("call 0x%0h", imm8);
                    `endif
                end
                default: begin
                    // 110,111: unused -> HALT
                    `ifdef SIM
                    $display("halt");
                    $finish;
                    `endif
                end
            endcase

            // Commit next PC
            pc <= next_pc;
        end
    end

endmodule