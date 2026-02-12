`define S_FETCH   2'b00
`define S_DECEXEC 2'b01
`define S_WB      2'b10

module minc (
    input  logic        clk,
    input  logic        reset_n,
    output logic [7:0]  pc_out,
    output logic [7:0]  sp_out,
    output logic [7:0]  address,
    output logic [7:0]  data_out,
    output logic        we,
    input  logic [7:0]  data_in
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
    // logic  [7:0]  ram  [0:255];

    // ROM load (one word per line, hex). TEST selects test.hex
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    // Outputs
    assign pc_out  = pc;
    assign sp_out  = sp;

    // Fetch current instruction (15-bit in [14:0])
    logic [14:0] instr;

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
    // wire [7:0] next_pc =    op == 3'b000 && subop == 4'b1100 ? ram[sp] :
    //                         op == 3'b100 ? (regs[rs] == 8'd0 ? imm8 : pc + 8'd1) :
    //                         op == 3'b101 ? imm8 :
    //                         op == 3'b110 ? (regs[rs] != 8'd0 ? imm8 : pc + 8'd1) :
    //                         op == 3'b111 ? pc :
    //                         pc + 8'd1;

    wire [7:0] alu_out =    subop == 4'b0000 ? rs_val :
                            subop == 4'b0001 ? rd_val + rs_val :
                            subop == 4'b0010 ? rd_val - rs_val :
                            subop == 4'b0011 ? (rd_val < rs_val ? 8'b1 : 8'b0) :
                            subop == 4'b0100 ? rd_val * rs_val :
                            8'h00; // default

    // wire [7:0] ram_addr =   op == 3'b000 ?
    //                             (subop == 4'b1000 ? sp - 8'd1 : // push
    //                             subop == 4'b1010 ? sp :         // pop
    //                             sp) :
    //                         op == 3'b101 ? sp - 8'd1 :          // call
    //                         (regs[15] + imm8);                  // ldm, stm

    // Main sequential logic
    always_ff @(posedge clk) begin
        if (!reset_n) begin
            pc <= 8'h00;
            sp <= 8'h00;
            state <= `S_FETCH;
            instr <= 15'h0000;
            // Clear registers for deterministic startup
            for (i = 0; i < 16; i = i + 1) begin
                regs[i] <= 8'h00;
            end
        end else begin
            case (state)
                `S_FETCH: begin
                    // Fetch instruction
                    instr <= rom[pc][14:0];
                    pc <= pc + 8'd1;
                    state <= `S_DECEXEC;
                end
                `S_DECEXEC: begin
                    // Decode and Execute
                    case (op)
                        3'b000: begin
                            // subop-based operations
                            casez (subop)
                                4'b0???: begin
                                    // ALU operations: rd = rd <op> rs
                                    data_out <= alu_out;
                                end
                                4'b1000: begin
                                    // push rs : (--sp) = rs
                                    address <= sp - 8'd1;
                                    data_out <= rs_val;
                                    we <= 1'b1;
                                end
                                4'b1001: begin
                                    // lds rs : SP = rs
                                    data_out <= rs_val;
                                end
                                4'b1010: begin
                                    // pop rd : rd = (SP++)
                                    address <= sp;
                                end
                                4'b1011: begin
                                    // sts rd : rd = SP
                                    data_out <= sp;
                                end
                                4'b1100: begin
                                    // ret : PC = (SP++) + 1
                                    address <= sp;
                                end
                                default: begin end
                            endcase
                        end
                        3'b001: begin
                            // mvi rd, n : rd = n
                            data_out <= imm8;
                        end
                        3'b010: begin
                            // stm n, rs : [r15 + n] = rs
                            address <= regs[15] + imm8;
                            data_out <= rs_val;
                            we <= 1'b1;
                        end
                        3'b011: begin
                            // ldm n, rd : rd = [r15 + n]
                            address <= regs[15] + imm8;
                        end
                        3'b100: begin
                            // jz n, rs : PC = n if rs == 0
                        end
                        3'b101: begin
                            // call n : (--sp) = PC; PC = n
                            address <= sp - 8'd1;
                            data_out <= pc;
                            we <= 1'b1;
                        end
                        default: begin
                            // 110,111: unused -> HALT
                            `ifdef SIM
                            $display("HALT encountered at PC=%h", pc - 8'd1);
                            $finish;
                            `endif
                            pc <= pc - 8'd1; // stay on HALT instruction
                        end
                    endcase
                    state <= `S_WB;
                end
                `S_WB: begin
                    // Writeback phase
                    case (op)
                        3'b000: begin
                            // subop-based operations
                            casez (subop)
                            4'b0???: begin
                                // ALU operations: rd = rd <op> rs
                                regs[rd] <= data_out;
                            end
                            4'b1000: begin
                                // push rs : (--sp) = rs
                                sp <= sp - 8'd1; // wrap naturally (8-bit)
                            end
                            4'b1001: begin
                                // lds rs : SP = rs
                                sp <= data_out;
                            end
                            4'b1010: begin
                                // pop rd : rd = (SP++)
                                regs[rd] <= data_in;
                                sp <= sp + 8'd1;
                            end
                            4'b1011: begin
                                // sts rd : rd = SP
                                regs[rd] <= data_out;
                            end
                            4'b1100: begin
                                // ret : PC = (SP++)
                                pc <= data_in;
                                sp <= sp + 8'd1;
                            end
                            default: begin end
                            endcase
                        end
                        3'b001: begin
                            // mvi rd, n : rd = n
                            regs[rd] <= data_out;
                        end
                        3'b010: begin
                            // stm n, rs : [r15 + n] = rs
                        end
                        3'b011: begin
                            // ldm n, rd : rd = [r15 + n]
                            regs[rd] <= data_in;
                        end
                        3'b100: begin
                            // jz n, rs : PC = n if rs == 0
                            if (regs[rs] == 8'd0) begin
                                pc <= imm8;
                            end
                        end
                        3'b101: begin
                            // call n : (--sp) = PC; PC = n
                            sp <= sp - 8'd1;
                            pc <= imm8;
                        end
                        default: begin
                            // 110,111: unused -> HALT
                        end
                    endcase
                    we <= 1'b0;
                    state <= `S_FETCH;
                end
                default: state <= `S_FETCH;
            endcase
        end
    end

endmodule