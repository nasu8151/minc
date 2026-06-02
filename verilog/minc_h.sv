`define S_FETCH   3'b000
`define S_ALU     3'b011
`define S_PUSH_POP 3'b101
`define S_PUSH_POP2 3'b110
`define S_DECEXEC 3'b001
`define S_WB      3'b010
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

    logic [2:0] state;

    logic wait_reg;

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
    logic [15:0] instr;

    logic carry_flag;
    logic carry_flag_next;

    logic [5:0] op6 = instr[15:10];
    logic [3:0] op4 = instr[15:12];
    logic [1:0] op2 = instr[15:14];
    logic [3:0] subop = instr[13:10];
    logic [3:0] rd = instr[7:4];
    logic [3:0] rs = instr[3:0];

    logic [7:0] imm8 = {instr[12:5], instr[3:0]};

    logic [7:0] rd_val = regs[rd];
    logic [7:0] rs_val = regs[rs];

    logic [7:0] alu_out;

    // ALU
    always_comb begin
        case (subop)
            4'b0000: begin alu_out = rs_val; carry_flag_next = 1'bx; end // MOV
            4'b0001: begin alu_out = rd_val | rs_val; carry_flag_next = 1'bx; end // OR
            4'b0010: begin alu_out = rd_val & rs_val; carry_flag_next = 1'bx; end // AND
            4'b0011: begin alu_out = rd_val ^ rs_val; carry_flag_next = 1'bx; end // XOR
            4'b0100: {carry_flag_next, alu_out} = rd_val + rs_val;
            4'b0101: {carry_flag_next, alu_out} = rd_val + rs_val + carry_flag;
            4'b0110: {carry_flag_next, alu_out} = rd_val - rs_val;
            4'b0111: {carry_flag_next, alu_out} = rd_val - rs_val + carry_flag;
            4'b1000: {carry_flag_next, alu_out} = (rd_val < rs_val) ? 8'b1 : 8'b0; // LT
            4'b1001: {carry_flag_next, alu_out} = (rd_val < rs_val - carry_flag) ? 8'b1 : 8'b0; // LTC
            4'b1010: {alu_out, carry_flag_next} = rs_val >> 1 | (carry_flag << 7); // ROR
            4'b1110: begin alu_out = rd_val * rs_val; carry_flag_next = 1'bx; end // MUL
            4'b1111: begin alu_out = (rd_val * rs_val) >> 8; carry_flag_next = 1'bx; end // MULH
            default: begin alu_out = rd_val; carry_flag_next = 1'bx; end
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
                    if (op2 == 2'b00) begin
                        state <= `S_ALU;
                    end else if (op4 == 4'b0111) begin
                        state <= `S_PUSH_POP;
                    end else begin
                        state <= `S_WB;
                    end
                end
                `S_ALU: begin
                    state <= `S_FETCH;
                end
                `S_PUSH_POP: begin
                    state <= `S_PUSH_POP2;
                end
                `S_PUSH_POP2: begin
                    state <= `S_FETCH;
                end
                `S_WB: begin
                    state <= `S_FETCH;
                end
                `S_WAIT: begin
                    if (wait_rel) state <= `S_FETCH;
                end
                default: state <= `S_FETCH;
            endcase
        end
    end

        


endmodule