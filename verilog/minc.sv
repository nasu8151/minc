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
    wire [3:0] rd_pair_hi = {rd[3:1], 1'b1}; // r0/1, r2/3, ..., r14/15
    wire [3:0] rs_pair_hi = {rs[3:1], 1'b1}; // r0/1, r2/3, ..., r14/15
    wire [7:0] rs1_val = regs[rs_pair_hi];
    wire [15:0] x_base = {8'h00, regs[13]};
    wire [15:0] y_base = {8'h00, regs[15]};

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

    always @* begin
        alu_out = rd_val;
        carry_flag_next = carry_flag;
        alu_sets_c = 1'b0;

        case (subop)
            4'b0000: alu_out = rs_val;
            4'b0001: alu_out = rd_val | rs_val;
            4'b0010: alu_out = rd_val & rs_val;
            4'b0011: alu_out = rd_val ^ rs_val;
            4'b0100: begin
                tmp9 = {1'b0, rd_val} + {1'b0, rs_val};
                alu_out = tmp9[7:0];
                carry_flag_next = tmp9[8];
                alu_sets_c = 1'b1;
            end
            4'b0101: begin
                tmp9 = {1'b0, rd_val} + {1'b0, rs_val} + {8'b0, carry_flag};
                alu_out = tmp9[7:0];
                carry_flag_next = tmp9[8];
                alu_sets_c = 1'b1;
            end
            4'b0110: begin
                tmp9 = {1'b0, rd_val} - {1'b0, rs_val};
                alu_out = tmp9[7:0];
                carry_flag_next = tmp9[8];
                alu_sets_c = 1'b1;
            end
            4'b0111: begin
                tmp9 = {1'b0, rd_val} - {1'b0, rs_val} + {8'b0, carry_flag};
                alu_out = tmp9[7:0];
                carry_flag_next = tmp9[8];
                alu_sets_c = 1'b1;
            end
            4'b1000: begin
                tmp9 = {1'b0, rd_val} - {1'b0, rs_val};
                alu_out = (rd_val < rs_val) ? 8'h01 : 8'h00;
                carry_flag_next = tmp9[8];
                alu_sets_c = 1'b1;
            end
            4'b1001: begin
                ltc_sum = $signed({rd_val[7], rd_val}) + $signed({rs_val[7], rs_val}) + $signed({8'b0, carry_flag});
                tmp9 = {1'b0, rd_val} + {1'b0, rs_val} + {8'b0, carry_flag};
                alu_out = ltc_sum[8] ? 8'h01 : 8'h00;
                carry_flag_next = tmp9[8];
                alu_sets_c = 1'b1;
            end
            4'b1010: begin
                alu_out = {carry_flag, rs_val[7:1]};
                carry_flag_next = rs_val[0];
                alu_sets_c = 1'b1;
            end
            4'b1110: alu_out = rd_val * rs_val;
            4'b1111: alu_out = (rd_val * rs_val) >> 8;
            default: begin
                alu_out = rd_val;
            end
        endcase
    end

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

    logic [15:0] mem_addr16;

    always @* begin
        pc_next = pc;
        sp_next = sp;
        state_next = state;
        instr_next = instr;
        wait_reg_next = wait_reg;
        carry_flag_seq_next = carry_flag;
        data_out_next = 8'h00;

        reg_we0 = 1'b0;
        reg_waddr0 = 4'h0;
        reg_wdata0 = 8'h00;
        reg_we1 = 1'b0;
        reg_waddr1 = 4'h0;
        reg_wdata1 = 8'h00;

        mem_addr16 = 16'h0000;
        address_next = 16'h0000;
        we_next = 1'b0;

        if (wait_req) begin
            wait_reg_next = 1'b1;
        end
        if (wait_rel) begin
            wait_reg_next = 1'b0;
        end

        case (state)
            `S_FETCH: begin
                if (pc < ROM_DEPTH) begin
                    instr_next = rom[pc];
                end else begin
                    instr_next = 16'hFFFF;
                end
                pc_next = pc + 16'd1;
                state_next = `S_DECEXEC;
            end

            `S_DECEXEC: begin
                if (is_halt) begin
                    state_next = `S_DECEXEC;
                end else begin
                    if (is_push) begin
                        mem_addr16 = sp - 16'd1;
                        address_next = mem_addr16;
                        data_out_next = rs1_val;
                        we_next = 1'b1;
                        sp_next = sp - 16'd1;
                    end else if (is_calr) begin
                        mem_addr16 = sp - 16'd1;
                        address_next = mem_addr16;
                        data_out_next = pc[15:8];
                        we_next = 1'b1;
                        sp_next = sp - 16'd1;
                    end else if (is_pop || is_ret) begin
                        mem_addr16 = sp;
                        address_next = mem_addr16;
                    end else if (is_stm_x) begin
                        mem_addr16 = x_base + simm8;
                        address_next = mem_addr16;
                        data_out_next = rd_val;
                        we_next = 1'b1;
                    end else if (is_stm_y) begin
                        mem_addr16 = y_base + simm8;
                        address_next = mem_addr16;
                        data_out_next = rd_val;
                        we_next = 1'b1;
                    end else if (is_ldm_x) begin
                        mem_addr16 = x_base + simm8;
                        address_next = mem_addr16;
                    end else if (is_ldm_y) begin
                        mem_addr16 = y_base + simm8;
                        address_next = mem_addr16;
                    end
                    state_next = `S_WB;
                end
            end

            `S_WB: begin
                if (is_push) begin
                    mem_addr16 = sp - 16'd1;
                    address_next = mem_addr16;
                    data_out_next = rs_val;
                    we_next = 1'b1;
                    sp_next = sp - 16'd1;
                end else if (is_calr) begin
                    mem_addr16 = sp - 16'd1;
                    address_next = mem_addr16;
                    data_out_next = pc[7:0];
                    we_next = 1'b1;
                    sp_next = sp - 16'd1;
                end else if (is_pop || is_ret) begin
                    mem_addr16 = sp + 16'd1;
                    address_next = mem_addr16;
                    sp_next = sp + 16'd1;
                end else if (is_stm_x) begin
                    mem_addr16 = x_base + simm8;
                    address_next = mem_addr16;
                    data_out_next = rd_val;
                    we_next = 1'b1;
                end else if (is_stm_y) begin
                    mem_addr16 = y_base + simm8;
                    address_next = mem_addr16;
                    data_out_next = rd_val;
                    we_next = 1'b1;
                end else if (is_ldm_x) begin
                    mem_addr16 = x_base + simm8;
                    address_next = mem_addr16;
                end else if (is_ldm_y) begin
                    mem_addr16 = y_base + simm8;
                    address_next = mem_addr16;
                end

                if (is_alu) begin
                    reg_we0 = 1'b1;
                    reg_waddr0 = rd;
                    reg_wdata0 = alu_out;
                    if (alu_sets_c) begin
                        carry_flag_seq_next = carry_flag_next;
                    end
                end else if (is_mvi) begin
                    reg_we0 = 1'b1;
                    reg_waddr0 = rd;
                    reg_wdata0 = imm8;
                end else if (is_lds) begin
                    reg_we0 = 1'b1;
                    reg_waddr0 = rd;
                    reg_wdata0 = sp[7:0];
                end else if (is_pop) begin
                    reg_we0 = 1'b1;
                    reg_waddr0 = rd;
                    reg_wdata0 = data_in;
                end else if (is_sts) begin
                    sp_next = {8'h00, rs_val};
                end

                if (is_ret) begin
                    pc_next[7:0] = data_in;
                end else if (is_jz) begin
                    if (rd_val == 8'h00) begin
                        pc_next = pc + simm8;
                    end
                end else if (is_jr) begin
                    pc_next = pc + simm12;
                end

                if (is_stf && instr[0]) begin
                    carry_flag_seq_next = 1'b1;
                end else if (is_clf && instr[0]) begin
                    carry_flag_seq_next = 1'b0;
                end

                if (is_pop || is_ret || is_calr || is_ldm) begin
                    if (is_mem_like && (wait_reg || wait_req)) begin
                        state_next = `S_WB;
                    end else begin
                        state_next = `S_WB2;
                    end
                end else begin
                    if (is_mem_like && (wait_reg || wait_req)) begin
                        state_next = `S_WB;
                    end else begin
                        state_next = `S_FETCH;
                    end
                end
            end

            `S_WB2: begin
                if (is_pop) begin
                    reg_we0 = 1'b1;
                    reg_waddr0 = rd_pair_hi;
                    reg_wdata0 = data_in;
                    sp_next = sp + 16'd1;
                end else if (is_ldm) begin
                    reg_we0 = 1'b1;
                    reg_waddr0 = rd;
                    reg_wdata0 = data_in;
                end else if (is_ret) begin
                    pc_next[15:8] = data_in;
                    sp_next = sp + 16'd1;
                end else if (is_calr) begin
                    pc_next = pc + simm12;
                end

                if ((is_pop || is_ret) && (wait_reg || wait_req)) begin
                    state_next = `S_WB2;
                end else begin
                    state_next = `S_FETCH;
                end
            end

            default: begin
                state_next = `S_FETCH;
            end
        endcase
    end

    assign address = address_next;
    assign we = we_next;
    assign data_out = data_out_next;

    always @(posedge clk) begin
        if (reset_n && state == `S_DECEXEC && is_halt) begin
            `ifdef SIM
            $display("HALT encountered at PC=%h", pc - 16'd1);
            $finish;
            `endif
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            pc <= 16'h0000;
        end else begin
            pc <= pc_next;
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            sp <= 16'h00FF;
        end else begin
            sp <= sp_next;
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            state <= `S_FETCH;
        end else begin
            state <= state_next;
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            instr <= 16'h0000;
        end else begin
            instr <= instr_next;
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            wait_reg <= 1'b0;
        end else begin
            wait_reg <= wait_reg_next;
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            carry_flag <= 1'b0;
        end else begin
            carry_flag <= carry_flag_seq_next;
        end
    end

    always_ff @(posedge clk) begin
        if (!reset_n) begin
            integer i;
            for (i = 0; i < 16; i = i + 1) begin
                regs[i] <= 8'h00;
            end
        end else begin
            if (reg_we0) begin
                regs[reg_waddr0] <= reg_wdata0;
            end
            if (reg_we1) begin
                regs[reg_waddr1] <= reg_wdata1;
            end
        end
    end

endmodule
