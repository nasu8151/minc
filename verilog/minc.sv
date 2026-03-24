`define S_FETCH   3'b000
`define S_DECEXEC 3'b001
`define S_WB      3'b010
`define S_WB2     3'b011
`define S_WAIT    3'b111

module minc (
    input  logic        clk,
    input  logic        reset_n,
    output logic [15:0]  pc_out,
    output logic [7:0]  sp_out,
    output logic [7:0]  address,
    output logic [7:0]  data_out,
    output logic        we,
    input  logic [7:0]  data_in,
    input  logic        wait_req,
    input  logic        wait_rel
);

    // PC, SP
    logic [15:0] pc;
    logic  [7:0] sp;

    logic [1:0] state;

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15]; /* synthesis syn_ramstyle = "distributed" */

    // Instruction ROM: 64k words x 15-bit (instruction is 15-bit)
    logic  [15:0] rom  [0:65535]; 

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
    wire [2:0] op3   = instr[14:12];
    wire [3:0] subop = instr[11:8];
    wire [3:0] rd    = op3[0] == 1'b1 ? instr[3:0] : instr[7:4];
    wire [3:0] rs    = instr[3:0];
    wire [7:0] imm8  = instr[11:4];

    wire [7:0] rd_val =       regs[rd];
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
    wire is_alu  = (op3 == 3'b000) && (subop[3] == 1'b0);
    wire is_push = (op3 == 3'b000) && (subop == 4'b1000);
    wire is_lds  = (op3 == 3'b000) && (subop == 4'b1001);
    wire is_pop  = (op3 == 3'b000) && (subop == 4'b1010);
    wire is_sts  = (op3 == 3'b000) && (subop == 4'b1011);
    wire is_ret  = (op3 == 3'b000) && (subop == 4'b1100);
    wire is_mvi  = (op3 == 3'b001);
    wire is_stm  = (op3 == 3'b010);
    wire is_ldm  = (op3 == 3'b011);
    wire is_jz   = (op3 == 3'b100);
    wire is_call = (op3 == 3'b101);

    // Determines address the processor emits
    assign address =  is_push ? sp :
                            is_pop  ? sp :
                            is_ret  ? sp :
                            (is_stm || is_ldm) ? (regs[15] + imm8) :
                            is_call ? sp : 8'h00;

    // Determines written data
    // ALU out, rs_val for PUSH/STM, sp for STS, imm8 for MVI, pc for CALL
    wire [7:0] data_out_next =  is_alu ? alu_out :
                                (is_push || is_lds || is_stm) ? rs_val :
                                is_sts ? sp :
                                is_mvi ? imm8 :
                                (is_call && (state == `S_DECEXEC)) ? pc[15:8] : 
                                (is_call && (state == `S_WB)) ? pc[7:0]  : 8'h00;

    // Write Enable flag
    assign we = (state == `S_WB | state == `S_WB2) && (is_push || is_stm || is_call);

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
                    data_out <= data_out_next;

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

                    if (is_push || is_call) begin
                        sp <= sp - 8'd1;
                    end else if (is_ret) begin
                        sp <= sp + 8'd1;
                    end

                    state <= `S_WB;
                end
                `S_WB: begin
                    data_out <= data_out_next;
                    // Writeback phase
                    // Update registers
                    if (is_alu || is_mvi || is_sts) begin
                        regs[rd] <= data_out;
                    end else if (is_pop || is_ldm) begin
                        regs[rd] <= data_in;
                    end

                    // Update SP
                    if (is_pop || is_ret) begin
                        sp <= sp + 8'd1;
                    end else if (is_call) begin
                        sp <= sp - 8'd1;
                    end else if (is_lds) begin
                        sp <= data_out;
                    end

                    // Update PC
                    if (is_ret) begin
                        pc[7:0] <= data_in;
                    end else if (is_jz) begin
                        if (rs_val == 8'd0) begin
                            pc <= pc + 16'(signed'(imm8));
                        end
                    end

                    if (wait_req)
                        state <= `S_WAIT;
                    else if (is_call || is_ret)
                        state <= `S_WB2;
                    else
                        state <= `S_FETCH;
                end
                `S_WB2: begin
                    // Update PC
                    if (is_ret)
                        pc[15:8] <= data_in;
                    else if (is_call)
                        pc <= pc + 16'(signed'({rs, imm8}));
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