`define S_FETCH   2'b00
`define S_DECEXEC 2'b01
`define S_WB      2'b10
`define S_WAIT    2'b11

module minc (
    input  logic        clk,
    input  logic        reset_n,
    output logic [7:0]  pc_out,
    output logic [7:0]  sp_out,
    output logic [7:0]  address,
    output logic [7:0]  data_out,
    output logic        we,
    input  logic [7:0]  data_in,
    input  logic        wait_req,
    input  logic        wait_rel
);

    // PC, SP
    logic  [7:0] pc;
    logic  [7:0] sp;

    logic [1:0] state;

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15]; /* synthesis syn_ramstyle = "distributed" */

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
    wire [3:0] op4   = instr[14:11];
    wire [2:0] op    = instr[14:12];
    wire [2:0] op3   = instr[14:12];
    wire [3:0] subop = instr[11:8];
    wire [3:0] rd    = op[0] == 1'b1 ? instr[3:0] : instr[7:4];
    wire [3:0] rs    = instr[3:0];
    wire [7:0] imm8  = instr[11:4];

wire [7:0] rd_val =       regs[op == 3'b000 ? rd : 4'd15];
wire [7:0] rs_val =       regs[rs];
    integer i;

    wire [7:0] alu_out =    subop == 4'b0000 ? rs_val :
                            subop == 4'b0001 ? rd_val + rs_val :
                            subop == 4'b0010 ? rd_val - rs_val :
                            subop == 4'b0011 ? (rd_val < rs_val ? 8'b1 : 8'b0) :
                            subop == 4'b0100 ? rd_val * rs_val :
                            subop == 4'b0101 ? rd_val | rs_val :
                            subop == 4'b0110 ? rd_val & rs_val :
                            subop == 4'b0111 ? rd_val ^ rs_val :
                            8'h00; // default

    // Decode flags 
    wire is_alu  = (op == 3'b000) && (subop[3] == 1'b0);
    wire is_push = (op == 3'b000) && (subop == 4'b1000);
    wire is_lds  = (op == 3'b000) && (subop == 4'b1001);
    wire is_pop  = (op == 3'b000) && (subop == 4'b1010);
    wire is_sts  = (op == 3'b000) && (subop == 4'b1011);
    wire is_ret  = (op == 3'b000) && (subop == 4'b1100);
    wire is_mvi  = (op == 3'b001);
    wire is_stm  = (op == 3'b010);
    wire is_ldm  = (op == 3'b011);
    wire is_jz   = (op == 3'b100);
    wire is_call = (op == 3'b101);

    // Determines address the processor emits
    wire [7:0] addr_next = is_push ? (sp - 8'd1) :
                           is_pop  ? sp :
                           is_ret  ? sp :
                           (is_stm || is_ldm) ? (regs[15] + imm8) :
                           is_call ? (sp - 8'd1) : 8'h00;

    // Determines written data
    // ALU out, rs_val for PUSH/STM, sp for STS, imm8 for MVI, pc for CALL
    wire [7:0] data_out_next = is_alu ? alu_out :
                               (is_push || is_lds || is_stm) ? rs_val :
                               is_sts ? sp :
                               is_mvi ? imm8 :
                               is_call ? pc : 8'h00;

    // Write Enable flag
    wire we_next = (state == `S_DECEXEC) && (is_push || is_stm || is_call);

    // Main sequential logic
    always_ff @(posedge clk) begin
        if (!reset_n) begin
            pc <= 8'h00;
            sp <= 8'h00;
            state <= `S_FETCH;
            instr <= 15'h0000;
        end else begin
            case (state)
                `S_FETCH: begin
                    // Fetch instruction
                    instr <= rom[pc][14:0];
                    pc <= pc + 8'd1;
                    state <= `S_DECEXEC;
                end
                `S_DECEXEC: begin
                    // Assign combinational routing outputs at execution stage
                    address  <= addr_next;
                    data_out <= data_out_next;
                    we       <= we_next;

                    // Decode and Execute - Halting if unknown instruction
                    if (!(is_alu || is_push || is_lds || is_pop || is_sts || is_ret || 
                          is_mvi || is_stm || is_ldm || is_jz || is_call)) begin
                        // 110,111: unused -> HALT
                        `ifdef SIM
                        $display("HALT encountered at PC=%h", pc - 8'd1);
                        $finish;
                        `endif
                        pc <= pc - 8'd1; // stay on HALT instruction
                    end
                    state <= `S_WB;
                end
                `S_WB: begin
                    // Writeback phase
                    // Update registers appropriately
                    if (is_alu || is_mvi || is_sts) begin
                        regs[rd] <= data_out;
                    end else if (is_pop || is_ldm) begin
                        regs[rd] <= data_in;
                    end

                    // Update SP
                    if (is_push || is_call) begin
                        sp <= sp - 8'd1;
                    end else if (is_pop || is_ret) begin
                        sp <= sp + 8'd1;
                    end else if (is_lds) begin
                        sp <= data_out;
                    end

                    // Update PC
                    if (is_ret) begin
                        pc <= data_in;
                    end else if (is_jz) begin
                        if (regs[rs] == 8'd0) begin
                            pc <= imm8;
                        end
                    end else if (is_call) begin
                        pc <= imm8;
                    end

                    we <= 1'b0;
                    if (wait_req)
                        state <= `S_WAIT;
                    else 
                        state <= `S_FETCH;
                end
                `S_WAIT: begin
                    if (wait_rel)
                        state <= `S_FETCH;
                end
                default: state <= `S_FETCH;
            endcase
        end
    end

endmodule