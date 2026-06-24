`define S_FETCH   3'b000
`define S_MA      3'b100
`define S_DECEXEC 3'b001
`define S_WB      3'b010

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
    input  logic        wait_req
);

    // PC, SP
    logic [15:0] pc;
    logic [15:0] sp;

    logic [2:0] state;

    logic wait_reg;

    // General purpose registers r0..r15 (8-bit)
    logic  [7:0]  regs [0:15]; /* synthesis syn_ramstyle = "distributed" */

    // Instruction ROM: 64k words x 15-bit (instruction is 15-bit)
    logic  [17:0] rom  [0:65535]; 
    wire [17:0] cur = rom[pc];

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

    logic [7:0] data_in_internal;

    logic carry_flag;
    logic carry_flag_next;

    wire [5:0] op6 = instr[17:12];
    wire [3:0] op4 = instr[17:14];
    wire [1:0] op2 = instr[17:16];
    wire [3:0] subop = instr[13:10];

    wire [3:0] rd = instr[7:4];
    wire [3:0] rs = instr[3:0];

    logic [3:0] ra;
    logic [3:0] rb;
    logic [3:0] rw;

    wire [7:0] imm8 = {instr[11:8], instr[3:0]};
    wire [15:0] imm16 = {instr[15:12], instr[7:4], instr[11:8], instr[3:0]};
    wire signed [15:0] simm8  = 16'($signed(imm8));
    wire signed [15:0] simm16 = 16'($signed(imm16));

    wire [7:0] rd_val = regs[rd];
    wire [7:0] rs_val = regs[rs];

    wire [7:0] ra_val = regs[ra];
    wire [7:0] rb_val = regs[rb];

    logic [7:0] alu_out;

    wire is_alu = (op4 == 4'b0000);
    wire is_mem_like = (op2 != 2'b00);

    wire is_mul    = (op6 == 6'b000100);
    wire is_mulh   = (op6 == 6'b000101);
    wire is_stf    = (op6 == 6'b001000);
    wire is_clf    = (op6 == 6'b001001);
    wire is_jz     = (op6 == 6'b001100);
    wire is_mvi    = (op6 == 6'b001110);

    wire is_stm_x  = (op6 == 6'b010000);
    wire is_ldm_x  = (op6 == 6'b010001);
    wire is_stm_y  = (op6 == 6'b010010);
    wire is_ldm_y  = (op6 == 6'b010011);
    wire is_stm_n  = (op6 == 6'b010100);
    wire is_ldm_n  = (op6 == 6'b010101);
    wire is_stm    = (op6[5:3] == 3'b010 && op6[0] == 0);
    wire is_ldm    = (op6[5:3] == 3'b010 && op6[0] == 1);
    wire is_push   = (op6 == 6'b011100);
    wire is_pop    = (op6 == 6'b011101);
    wire is_ret    = (op6 == 6'b011111);
    wire is_calr   = (op2 == 2'b10);
    wire is_jr     = (op2 == 2'b11);

    // ALU
    always_comb begin
        case (subop)
            4'b0000: begin alu_out = rb_val; carry_flag_next = 1'bx; end // MOV
            4'b0001: begin alu_out = ra_val | rb_val; carry_flag_next = 1'bx; end // OR
            4'b0010: begin alu_out = ra_val & rb_val; carry_flag_next = 1'bx; end // AND
            4'b0011: begin alu_out = ra_val ^ rb_val; carry_flag_next = 1'bx; end // XOR
            4'b0100: {carry_flag_next, alu_out} = ra_val + rb_val; // ADD
            4'b0101: {carry_flag_next, alu_out} = ra_val + rb_val + carry_flag; // ADC
            4'b0110: {carry_flag_next, alu_out} = ra_val + {1'b0, ~rb_val} + 1'b1; // SUB
            4'b0111: {carry_flag_next, alu_out} = ra_val + {1'b0, ~rb_val} + carry_flag; // SBC
            4'b1000: {carry_flag_next, alu_out} = (ra_val < rb_val) ? 8'b1 : 8'b0; // LT
            4'b1001: {carry_flag_next, alu_out} = (ra_val < rb_val - carry_flag) ? 8'b1 : 8'b0; // LTC
            4'b1011: {alu_out, carry_flag_next} = rb_val >> 1 | (carry_flag << 7); // ROR
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
                    if (is_alu) begin 
                        carry_flag <= carry_flag_next;
                        state <= `S_FETCH;
                    end
                    else if (is_mem_like) state <= `S_MA;
                    else state <= `S_WB;
                end
                `S_MA: begin
                    state <= `S_WB;
                end
                `S_WB: begin
                    state <= `S_FETCH;
                end
                default: state <= `S_FETCH;
            endcase
        end
    end
    assign we = ((is_calr || is_stm || is_push) && (state == `S_WB)) 
                || ((is_calr) && (state == `S_MA)) ? 1'b1 : 1'b0;
    assign avma = (is_stm || is_ldm) && (state == `S_MA);

    // PC and ROM control
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            pc <= 16'd0;
        end else begin
            case (state)
                `S_FETCH: begin
                    instr <= cur;
                    pc <= pc + 16'd1;
                end
                `S_MA: begin
                    if (is_ret) begin
                        pc[7:0] <= data_in_internal;
                    end
                end
                `S_WB: begin
                    if (is_jz) begin
                        if (ra_val == 8'd0) pc <= pc + simm8;
                    end else if (is_jr || is_calr) begin
                        pc <= pc + simm16;
                    end else if (is_ret) begin
                        pc[15:8] <= data_in_internal;
                    end
`ifdef SIM
                    if (instr == 18'h3FFFF) $finish;
`endif
                end
            endcase
        end
    end

    // SP and AGU
    logic [15:0] addr_base;
    logic [15:0] addr_latch;
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            sp <= 16'd0;
        end else begin
            case (state)
                `S_DECEXEC: begin
                    if (is_stm_x || is_ldm_x || is_stm_y || is_ldm_y) begin
                        addr_base <= {ra_val, rb_val};
                    end else if (is_stm_n || is_ldm_n) begin
                        addr_base <= 16'd0;
                    end
                    if (is_calr) begin
                        sp <= sp - 16'd1;
                    end
                    if (is_ret) begin
                        sp <= sp + 16'd1;
                    end
                end
                `S_MA: begin
                    if (is_push || is_calr) begin
                        sp <= sp - 16'd1;
                    end
                    if (is_pop || is_ret) begin
                        sp <= sp + 16'd1;
                    end
                end
            endcase
            if (we && (address == 16'h0000))
                sp[7:0] <= data_out;
            else if (we && (address == 16'h0001))
                sp[15:8] <= data_out;
        end
        addr_latch <= address;
    end
    assign address =    (is_ldm || is_stm) ? addr_base + simm8 : 
                        (is_calr || is_push || is_pop) ? sp : 
                        (is_ret) ? sp : 16'hxxxx;

    assign data_in_internal =   (addr_latch == 16'h0000) ? sp[7:0] :
                                (addr_latch == 16'h0001) ? sp[15:8] : data_in;

    always_ff @( posedge clk or negedge reset_n ) begin
        if (!reset_n) begin
            data_out <= 8'hxx;
        end else begin
            if (state == `S_DECEXEC) begin
                data_out <= (is_calr) ? pc[15:8] : 8'hxx;
            end else if (state == `S_MA) begin
            data_out <= (is_push) ? ra_val :
                            (is_stm)  ? ra_val :
                            (is_calr) ? pc[7:0] : 8'hxx;
            end else begin
                data_out <= 8'hxx;
            end
        end
    end

    wire [7:0] rw_next =   (is_alu) ? alu_out : 
                            (is_mvi) ? imm8 : 
                            (is_pop || is_ldm) ? data_in_internal : 8'hxx;
    // Register file
    
    assign ra = (is_ldm_x || (is_stm_x && (state == `S_DECEXEC))) ? 4'd13 :
                (is_ldm_y || (is_stm_y && (state == `S_DECEXEC))) ? 4'd15 : rd;
    assign rb = (is_ldm_x || is_stm_x) ? 4'd12 :
                (is_ldm_y || is_stm_y) ? 4'd14 : rs;
    assign rw = rd;

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            // Nothing
        end else begin
            case (state)
                `S_DECEXEC: begin
                    if (is_alu) regs[rw] <= rw_next;
                end
                `S_WB: begin
                    if (is_mvi || is_pop || is_ldm) regs[rw] <= rw_next;
                end
            endcase
        end
    end



endmodule