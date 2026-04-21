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

    logic [15:0] pc;
    logic [15:0] sp;
    logic [2:0]  state;
    logic        wait_reg;

    logic [7:0] regs [0:15]; /* synthesis syn_ramstyle = "distributed" */
    `ifdef SIM
    localparam int ROM_DEPTH = 4096;
    `else
    localparam int ROM_DEPTH = 65536;
    `endif
    logic [15:0] rom [0:ROM_DEPTH-1];

    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    assign pc_out = pc;
    assign sp_out = sp;

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
    wire signed [15:0] simm8  = $signed({{8{imm8[7]}}, imm8});
    wire signed [15:0] simm12 = $signed({{4{rel12[11]}}, rel12});

    wire [7:0] rd_val = regs[rd];
    wire [7:0] rs_val = regs[rs];
    wire [3:0] rd_pair_hi = rd | 4'b0001;
    wire [3:0] rs_pair_hi = rs | 4'b0001;
    wire [7:0] rs1_val = regs[rs_pair_hi];
    wire [15:0] x_base = {regs[12], regs[13]};
    wire [15:0] y_base = {regs[14], regs[15]};

    wire is_alu    = (op6[5:4] == 2'b00);
    wire is_stf    = (op6 == 6'b010000);
    wire is_clf    = (op6 == 6'b010001);
    wire is_push   = (op6 == 6'b011100);
    wire is_pop_op = (op6 == 6'b011101);
    wire is_ret    = is_pop_op && (instr[7:0] == 8'h10);
    wire is_pop    = is_pop_op && !is_ret;
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
    logic        alu_sets_c;
    logic [8:0]  tmp9;
    logic signed [8:0] ltc_sum;

    logic [15:0] pc_next;
    logic [15:0] sp_next;
    logic [2:0]  state_next;
    logic [15:0] instr_next;
    logic        wait_reg_next;
    logic        carry_flag_seq_next;
    logic [7:0]  data_out_next;

    logic        reg_we0;
    logic [3:0]  reg_waddr0;
    logic [7:0]  reg_wdata0;
    logic        reg_we1;
    logic [3:0]  reg_waddr1;
    logic [7:0]  reg_wdata1;

    logic [15:0] address_next;
    logic        we_next;

    wire in_fetch   = (state == `S_FETCH);
    wire in_decexec = (state == `S_DECEXEC);
    wire in_wb      = (state == `S_WB);
    wire in_wb2     = (state == `S_WB2);
    wire mem_wait   = (wait_reg || wait_req);

    always @* begin
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

    always @* begin
        instr_next = instr;
        if (in_fetch) begin
            if (pc < ROM_DEPTH) instr_next = rom[pc];
            else instr_next = 16'hFFFF;
        end
    end

    always @* begin
        wait_reg_next = wait_reg;
        if (wait_req) wait_reg_next = 1'b1;
        if (wait_rel) wait_reg_next = 1'b0;
    end

    always @* begin
        state_next = state;
        case (state)
            `S_FETCH: begin
                state_next = `S_DECEXEC;
            end
            `S_DECEXEC: begin
                if (is_halt) state_next = `S_DECEXEC;
                else state_next = `S_WB;
            end
            `S_WB: begin
                if (is_pop || is_ret || is_calr || is_ldm) begin
                    if (is_mem_like && mem_wait) state_next = `S_WB;
                    else state_next = `S_WB2;
                end else begin
                    if (is_mem_like && mem_wait) state_next = `S_WB;
                    else state_next = `S_FETCH;
                end
            end
            `S_WB2: begin
                if ((is_pop || is_ret || is_ldm) && mem_wait) state_next = `S_WB2;
                else state_next = `S_FETCH;
            end
            default: begin
                state_next = `S_FETCH;
            end
        endcase
    end

    always @* begin
        if (in_fetch) begin
            pc_next = pc + 16'd1;
        end else if (in_wb) begin
            if (is_ret) begin
                pc_next[7:0] = data_in;
            end else if (is_jz) begin
                if (rd_val == 8'h00) pc_next = pc + simm8;
            end else if (is_jr) begin
                pc_next = pc + simm12;
            end
        end else if (in_wb2) begin
            if (is_ret) begin
                pc_next[15:8] = data_in;
            end else if (is_calr) begin
                pc_next = pc + simm12;
            end
        end
    end

    always @* begin
        sp_next = sp;
        if (in_decexec) begin
            if (is_push || is_calr) sp_next = sp - 16'd1;
        end else if (in_wb) begin
            if (is_push || is_calr) sp_next = sp - 16'd1;
            else if (is_pop || is_ret) sp_next = sp + 16'd1;
            else if (is_sts) sp_next = {rs1_val, rs_val};
        end else if (in_wb2) begin
            if (is_pop || is_ret) sp_next = sp + 16'd1;
        end
    end

    always @* begin
        address_next = 16'h0000;
        we_next = 1'b0;
        data_out_next = 8'h00;

        if (in_decexec) begin
            if (is_push) begin
                address_next = sp - 16'd1;
                we_next = 1'b1;
                data_out_next = rs1_val;
            end else if (is_calr) begin
                address_next = sp - 16'd1;
                we_next = 1'b1;
                data_out_next = pc[15:8];
            end else if (is_pop || is_ret) begin
                address_next = sp;
            end else if (is_stm_x) begin
                address_next = x_base + simm8;
                we_next = 1'b1;
                data_out_next = rd_val;
            end else if (is_stm_y) begin
                address_next = y_base + simm8;
                we_next = 1'b1;
                data_out_next = rd_val;
            end else if (is_ldm_x) begin
                address_next = x_base + simm8;
            end else if (is_ldm_y) begin
                address_next = y_base + simm8;
            end
        end else if (in_wb) begin
            if (is_push) begin
                address_next = sp - 16'd1;
                we_next = 1'b1;
                data_out_next = rs_val;
            end else if (is_calr) begin
                address_next = sp - 16'd1;
                we_next = 1'b1;
                data_out_next = pc[7:0];
            end else if (is_pop || is_ret) begin
                address_next = sp + 16'd1;
            end else if (is_stm_x) begin
                address_next = x_base + simm8;
                we_next = 1'b1;
                data_out_next = rd_val;
            end else if (is_stm_y) begin
                address_next = y_base + simm8;
                we_next = 1'b1;
                data_out_next = rd_val;
            end else if (is_ldm_x) begin
                address_next = x_base + simm8;
            end else if (is_ldm_y) begin
                address_next = y_base + simm8;
            end
        end
    end

    always @* begin
        reg_we0 = 1'b0;
        reg_waddr0 = 4'h0;
        reg_wdata0 = 8'h00;
        reg_we1 = 1'b0;
        reg_waddr1 = 4'h0;
        reg_wdata1 = 8'h00;

        if (in_wb) begin
            if (is_alu) begin
                reg_we0 = 1'b1;
                reg_waddr0 = rd;
                reg_wdata0 = alu_out;
            end else if (is_mvi) begin
                reg_we0 = 1'b1;
                reg_waddr0 = rd;
                reg_wdata0 = imm8;
            end else if (is_lds) begin
                reg_we0 = 1'b1;
                reg_waddr0 = rd;
                reg_wdata0 = sp[7:0];
                reg_we1 = 1'b1;
                reg_waddr1 = rd_pair_hi;
                reg_wdata1 = sp[15:8];
            end else if (is_pop) begin
                reg_we0 = 1'b1;
                reg_waddr0 = rd;
                reg_wdata0 = data_in;
            end
        end else if (in_wb2) begin
            if (is_pop) begin
                reg_we0 = 1'b1;
                reg_waddr0 = rd_pair_hi;
                reg_wdata0 = data_in;
            end else if (is_ldm) begin
                reg_we0 = 1'b1;
                reg_waddr0 = rd;
                reg_wdata0 = data_in;
            end
        end
    end

    always @* begin
        carry_flag_seq_next = carry_flag;

        if (in_wb && is_alu && alu_sets_c) begin
            carry_flag_seq_next = carry_flag_next;
        end
        if (in_wb && is_stf && instr[0]) begin
            carry_flag_seq_next = 1'b1;
        end else if (in_wb && is_clf && instr[0]) begin
            carry_flag_seq_next = 1'b0;
        end
    end

    assign address = address_next;
    assign we = we_next;
    assign data_out = data_out_next;

    always @(posedge clk) begin
        if (reset_n && in_decexec && is_halt) begin
            `ifdef SIM
            $display("HALT encountered at PC=%h", pc - 16'd1);
            $finish;
            `endif
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) pc <= 16'h0000;
        else pc <= pc_next;
    end

    always_ff @(posedge clk) begin
        if (!reset_n) sp <= 16'h00FF;
        else sp <= sp_next;
    end

    always_ff @(posedge clk) begin
        if (!reset_n) state <= `S_FETCH;
        else state <= state_next;
    end

    always_ff @(posedge clk) begin
        if (!reset_n) instr <= 16'h0000;
        else instr <= instr_next;
    end

    always_ff @(posedge clk) begin
        if (!reset_n) wait_reg <= 1'b0;
        else wait_reg <= wait_reg_next;
    end

    always_ff @(posedge clk) begin
        if (!reset_n) carry_flag <= 1'b0;
        else carry_flag <= carry_flag_seq_next;
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            integer i;
            for (i = 0; i < 16; i = i + 1) regs[i] <= 8'h00;
        end else begin
            if (reg_we0) regs[reg_waddr0] <= reg_wdata0;
            if (reg_we1) regs[reg_waddr1] <= reg_wdata1;
        end
    end

endmodule
