`define S_FETCH   3'b000
`define S_DECEXEC 3'b001
`define S_WB      3'b010
`define S_WB2     3'b011

module minc (
    input  logic        clk,
    input  logic        reset_n,
    output logic [15:0] pc_out,
    output logic [15:0] sp_out,
    output logic [15:0] address,
    output logic [7:0]  data_out,
    output logic        we,
    input  logic [7:0]  data_in,
    input  logic        wait_req,
    input  logic        wait_rel
);

    // PC, SP
    logic [15:0] pc;
    logic [14:0] sp;
    logic        sp0;

    logic [2:0] state;

    logic wait_reg;

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15]; /* synthesis syn_ramstyle = "distributed" */
    logic [3:0] ra;
    logic [3:0] rb;
    logic [3:0] rw;
    wire [7:0]  ra_val = regs[ra];
    wire [7:0]  rb_val = regs[rb];

    // Instruction ROM: 64k words x 15-bit (instruction is 15-bit)
    logic  [15:0] rom  [0:65535]; 

    // ROM load (one word per line, hex). TEST selects test.hex
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    assign pc_out = pc;
    assign sp_out = {sp, 1'b0};

    logic [15:0] instr;

    logic carry_flag;
    logic carry_flag_next;

    wire [5:0] op6      = instr[15:10];
    wire [3:0] op4      = instr[15:12];
    wire [3:0] subop    = instr[13:10];
    wire [3:0] rd       = instr[7:4];
    wire [3:0] rs       = instr[3:0];
    wire [7:0] imm8     = {instr[11:8], instr[3:0]};
    wire [11:0] rel12   = {instr[7:4], instr[11:8], instr[3:0]};
    wire signed [15:0] simm8  = 16'($signed(imm8));
    wire signed [15:0] simm12 = 16'($signed(rel12));

    wire [3:0] rd_pair_hi = rd | 4'b0001;
    wire [3:0] rs_pair_hi = rs | 4'b0001;
    wire [7:0] rd_val = regs[rd];
    wire [7:0] rs_val = regs[rs];
    // wire [7:0] rb_val_hi = regs[rs_pair_hi];
    // wire [7:0] ra_val_hi = regs[rd_pair_hi];
    logic  [15:0] addr_base;

    wire is_alu    = (op6[5:4] == 2'b00);
    wire is_stf    = (op6 == 6'b010000);
    wire is_clf    = (op6 == 6'b010001);
    wire is_push   = (op6 == 6'b011100);
    wire is_pop_op = (op6 == 6'b011101);
    wire is_ret    = is_pop_op && (rd[0] == 1'b1); // Odd register number indicates RET
    wire is_pop    = is_pop_op && (rd[0] == 1'b0); // Even register number indicates POP
    wire is_sts    = (op6 == 6'b011110);
    wire is_lds    = (op6 == 6'b011111);

    wire is_stm_x  = (op4 == 4'b1000);
    wire is_ldm_x  = (op4 == 4'b1001);
    wire is_stm_y  = (op4 == 4'b1010);
    wire is_ldm_y  = (op4 == 4'b1011);
    wire is_stm    = is_stm_x || is_stm_y;
    wire is_ldm    = is_ldm_x || is_ldm_y;
    wire is_mvi    = (op4 == 4'b1100);
    wire is_jz     = (op4 == 4'b1101);
    wire is_calr   = (op4 == 4'b1110);
    wire is_jr     = (op4 == 4'b1111);

    wire is_mem_like = is_push || is_pop || is_ret || is_stm || is_ldm || is_calr;
    wire is_halt = (instr == 16'hFFFF);

    logic [7:0]  alu_out;
    logic [15:0] pc_next;
    // logic [14:0] sp_next;
    logic        wait_reg_next;

    assign sp0 =    ((is_push || is_calr) && (state == `S_WB)) ? 1'b1 :
                    ((is_pop || is_ret) && (state == `S_DECEXEC)) ? 1'b1 : 1'b0;
    wire [15:0] sp_val = {sp, sp0};

    always_comb begin : ALU
        case (subop)
            4'b0000: begin alu_out = rb_val; carry_flag_next = 1'bx; end // MOV
            4'b0001: begin alu_out = ra_val | rb_val; carry_flag_next = 1'bx; end // OR
            4'b0010: begin alu_out = ra_val & rb_val; carry_flag_next = 1'bx; end // AND
            4'b0011: begin alu_out = ra_val ^ rb_val; carry_flag_next = 1'bx; end // XOR
            4'b0100: {carry_flag_next, alu_out} = ra_val + rb_val;
            4'b0101: {carry_flag_next, alu_out} = ra_val + rb_val + carry_flag;
            4'b0110: {carry_flag_next, alu_out} = ra_val + {1'b0, ~rb_val} + 1'b1; // SUB
            4'b0111: {carry_flag_next, alu_out} = ra_val + {1'b0, ~rb_val} + carry_flag;
            4'b1000: {carry_flag_next, alu_out} = (ra_val < rb_val) ? 8'b1 : 8'b0; // LT
            4'b1001: {carry_flag_next, alu_out} = (ra_val < rb_val - carry_flag) ? 8'b1 : 8'b0; // LTC
            4'b1010: {alu_out, carry_flag_next} = rb_val >> 1 | (carry_flag << 7); // ROR
            4'b1110: begin alu_out = ra_val * rb_val; carry_flag_next = 1'bx; end // MUL
            4'b1111: begin alu_out = ({8'd0, ra_val} * {8'd0, rb_val}) >> 8; carry_flag_next = 1'bx; end // MULH
            default: begin alu_out = ra_val; carry_flag_next = 1'bx; end
        endcase
    end

    // Determines address the processor emits
    assign address =    is_push ? sp_val :
                        is_pop  ? sp_val :
                        is_ret  ? sp_val :
                        (is_stm || is_ldm) ? addr_base + simm8 :
                        is_calr ? sp_val : 16'hxxxx;

    // Determines written data
    // ALU out, rb_val for PUSH/STM, sp for STS, imm8 for MVI, pc for CALL
    wire [7:0] data_out_next =  (is_push) ? ra_val :
                                (is_stm)  ? ra_val :
                                (is_calr && (state == `S_WB)) ? pc[15:8] : 
                                (is_calr && (state == `S_DECEXEC)) ? pc[7:0]  : 8'hxx;
    
    wire [7:0] rw_next =    is_alu ? alu_out :
                            (is_lds && (state == `S_WB))  ? sp[14:7]  :
                            (is_lds && (state == `S_WB2)) ? {sp[6:0], 1'b0} :
                            is_mvi ? imm8 : 
                            (is_pop || is_ldm) ? data_in : 8'hxx;

    // Write Enable
    assign we = (state == `S_WB && (is_push || is_calr)) || (state == `S_WB2 && (is_stm || is_push || is_calr));

    // Main sequential logic
    always_ff @(posedge clk) begin
        if (!reset_n) begin
            pc <= 16'h0000;
            sp <= 15'h0000;
            state <= `S_FETCH;
            wait_reg <= 1'b0;
            instr <= 16'h0000;
        end else begin
            case (state)
                `S_FETCH: begin
                    // Fetch instruction
                    instr <= rom[pc][15:0];
                    pc <= pc + 8'd1;
                    state <= `S_DECEXEC;
                end
                `S_DECEXEC: begin
                    // Assign combinational routing outputs at execution stage
                    data_out <= data_out_next;

                    // Decode and Execute - Halting if unknown instruction
                    if (instr == 16'hFFFF) begin
                        `ifdef SIM
                        $display("HALT encountered at PC=%h", pc - 8'd1);
                        $finish;
                        `endif
                    end
                    if (is_push || is_calr) begin
                        sp <= sp - 15'd1;
                    end else if (is_sts) begin
                        sp <= {ra_val[7:0], sp[6:0]};
                    end

                    if (is_stm || is_ldm) begin 
                        addr_base <= {ra_val, rb_val};
                    end

                    if (is_alu || is_mvi) begin
                        regs[rw] <= rw_next;
                        carry_flag <= carry_flag_next;
                    end

                    if (is_jz) begin
                        if (ra_val == 8'd0) begin
                            pc <= pc + simm8;
                        end
                    end else if (is_jr) begin
                        pc <= pc + simm12;
                    end

                    if (is_alu || is_mvi || is_jr || is_jz) begin
                        state <= `S_FETCH;
                    end else begin
                        state <= `S_WB;
                    end
                end
                `S_WB: begin
                    data_out <= data_out_next;
                    // Writeback phase
                    // Update registers
                    if (is_pop || is_lds || is_ldm)
                        regs[rw] <= rw_next;

                    // Update PC
                    if (is_ret) begin
                        pc[7:0] <= data_in;
                    end

                    if (is_sts) begin
                        sp <= {sp[14:7], ra_val[7:1]};
                    end

                    if (is_calr || is_ret || is_push || is_pop || is_lds || is_sts || is_ldm || is_stm) begin
                        state <= `S_WB2;
                    end else begin
                        state <= `S_FETCH;
                    end
                end
                `S_WB2: begin
                    if (is_pop || is_lds || is_ldm)
                        regs[rw] <= rw_next;

                    // Update SP
                    if (is_pop || is_ret) begin
                        sp <= sp + 15'd1;
                    end

                    // Update PC
                    if (is_ret)
                        pc[15:8] <= data_in;
                    else if (is_calr)
                        pc <= pc + simm12;

                    if (wait_req)
                        wait_reg <= 1'b1;
                    if (wait_rel)
                        wait_reg <= 1'b0;

                    if (wait_reg || wait_req) begin
                        state <= `S_WB;
                    end else begin
                        state <= `S_FETCH;
                    end
                end
                // `S_WAIT: begin
                //     if (wait_rel)
                //         state <= `S_FETCH;
                // end
                default: state <= `S_FETCH;
            endcase
        end
    end

    always_comb begin
        case (state)
            `S_DECEXEC: begin
                if (is_stm_x || is_ldm_x)      ra = 4'd13;
                else if (is_stm_y || is_ldm_y) ra = 4'd15;
                else if (is_push || is_sts)    ra = rd_pair_hi;
                else                           ra = rd;
            end
            `S_WB: begin
                ra = rd;
            end
            `S_WB2: begin
                ra = rd;
            end
            default: ra = 4'hx;
        endcase
        if (is_stm_x || is_ldm_x)      rb = 4'd12;
        else if (is_stm_y || is_ldm_y) rb = 4'd14;
        else                           rb = rs;
        case (state)
            `S_DECEXEC: begin
                rw = rd;
            end
            `S_WB: begin
                rw = rd_pair_hi;
            end
            `S_WB2: begin
                rw = rd;
            end
            default: rw = 4'hx;
        endcase
    end

endmodule
