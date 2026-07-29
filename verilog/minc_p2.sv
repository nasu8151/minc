// minc CPU - PicoBlaze-style 2-stage pipeline (Fetch // Execute), binary compatible with minc_h.sv.
//
// Key property: only one in-flight instruction ever touches the register file / SP / carry_flag
// (the one occupying the Execute stage). Fetch never reads architectural state, so no RAW
// forwarding network is required - only structural stalls (Execute busy) and branch-flush
// bubbles (always-flush, no prediction) are needed.

`define XS_DE 2'b00
`define XS_MA 2'b01
`define XS_WB 2'b10

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

    // Architectural state
    logic [15:0] pc;   // address of the next instruction to fetch
    logic [15:0] sp;
    logic  [7:0] regs [0:15]; /* synthesis syn_ramstyle = "distributed" */
    logic        carry_flag;

    // Instruction ROM
    logic [17:0] rom [0:65535];
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    assign pc_out = pc;
    assign sp_out = sp;

    // ---------------------------------------------------------------
    // Fetch-stage skid buffer (depth 1) and Execute-stage instruction latch
    // ---------------------------------------------------------------
    logic        buf_valid;
    logic [17:0] buf_instr;
    logic [15:0] buf_pc_next;

    logic        cur_valid;
    logic [17:0] cur_instr;
    logic [15:0] cur_pc_next;   // = (address of cur_instr) + 1, matches minc_h.sv's `pc` invariant
    logic [1:0]  x_state;

    // ---------------------------------------------------------------
    // Decode of the instruction currently in Execute
    // ---------------------------------------------------------------
    wire [17:0] instr = cur_instr;
    wire [5:0] op6   = instr[17:12];
    wire [3:0] op4    = instr[17:14];
    wire [1:0] op2    = instr[17:16];
    wire [3:0] subop  = instr[13:10];
    wire [3:0] rd     = instr[7:4];
    wire [3:0] rs     = instr[3:0];
    wire [7:0] imm8   = {instr[11:8], instr[3:0]};
    wire [15:0] imm16 = {instr[15:12], instr[7:4], instr[11:8], instr[3:0]};
    wire signed [15:0] simm8  = 16'($signed(imm8));
    wire signed [15:0] simm16 = 16'($signed(imm16));

    wire is_alu   = (op4 == 4'b0000);
    wire is_jz    = (op6 == 6'b001100);
    wire is_mvi   = (op6 == 6'b001110);
    wire is_stm_x = (op6 == 6'b010000);
    wire is_ldm_x = (op6 == 6'b010001);
    wire is_stm_y = (op6 == 6'b010010);
    wire is_ldm_y = (op6 == 6'b010011);
    wire is_stm_n = (op6 == 6'b010100);
    wire is_ldm_n = (op6 == 6'b010101);
    wire is_stm   = (op6[5:3] == 3'b010 && op6[0] == 0);
    wire is_ldm   = (op6[5:3] == 3'b010 && op6[0] == 1);
    wire is_push  = (op6 == 6'b011100);
    wire is_pop   = (op6 == 6'b011101);
    wire is_ret   = (op6 == 6'b011111);
    wire is_calr  = (op2 == 2'b10);
    wire is_jr    = (op2 == 2'b11);
    wire is_mem2  = is_push || is_pop || is_calr || is_ret; // fixed 2-byte, never honors wait_req

    wire is_x_mode = is_stm_x || is_ldm_x;
    wire is_y_mode = is_stm_y || is_ldm_y;

    wire [7:0] rd_val    = regs[rd];
    wire [7:0] rs_val    = regs[rs];
    wire [7:0] ptr_hi_val = is_x_mode ? regs[4'd13] : is_y_mode ? regs[4'd15] : 8'h00;
    wire [7:0] ptr_lo_val = is_x_mode ? regs[4'd12] : is_y_mode ? regs[4'd14] : 8'h00;

    logic [7:0] alu_out;
    logic       carry_flag_next;

    always_comb begin
        case (subop)
            4'b0000: begin alu_out = rs_val; carry_flag_next = 1'bx; end // MOV
            4'b0001: begin alu_out = rd_val | rs_val; carry_flag_next = 1'bx; end // OR
            4'b0010: begin alu_out = rd_val & rs_val; carry_flag_next = 1'bx; end // AND
            4'b0011: begin alu_out = rd_val ^ rs_val; carry_flag_next = 1'bx; end // XOR
            4'b0100: {carry_flag_next, alu_out} = rd_val + rs_val; // ADD
            4'b0101: {carry_flag_next, alu_out} = rd_val + rs_val + carry_flag; // ADC
            4'b0110: {carry_flag_next, alu_out} = rd_val + {1'b0, ~rs_val} + 1'b1; // SUB
            4'b0111: {carry_flag_next, alu_out} = rd_val + {1'b0, ~rs_val} + carry_flag; // SBC
            4'b1000: {carry_flag_next, alu_out} = (rd_val < rs_val) ? 8'b1 : 8'b0; // LT
            4'b1001: {carry_flag_next, alu_out} = (rd_val < rs_val - carry_flag) ? 8'b1 : 8'b0; // LTC
            4'b1011: {alu_out, carry_flag_next} = rs_val >> 1 | (carry_flag << 7); // ROR
            4'b1110: begin alu_out = rd_val * rs_val; carry_flag_next = 1'bx; end // MUL
            4'b1111: begin alu_out = (rd_val * rs_val) >> 8; carry_flag_next = 1'bx; end // MULH
            default: begin alu_out = rd_val; carry_flag_next = 1'bx; end
        endcase
    end

    wire [7:0] rw_next = is_alu ? alu_out :
                          is_mvi ? imm8 :
                          (is_pop || is_ldm) ? data_in_internal : rd_val;

    // ---------------------------------------------------------------
    // Address generation / SP-relative addressing
    // ---------------------------------------------------------------
    logic [15:0] addr_base;
    logic [15:0] addr_latch;
    logic [7:0]  ret_pc_lo;

    wire jz_taken = is_jz && (rd_val == 8'd0);
    wire [15:0] jz_target = cur_pc_next + simm8;
    wire [15:0] jmp_target = cur_pc_next + simm16; // jr and calr share this

    // Fires exactly once, while the branch/jump is in its resolving cycle
    wire flush_jz   = cur_valid && (x_state == `XS_DE) && is_jz && jz_taken;
    wire flush_jr   = cur_valid && (x_state == `XS_DE) && is_jr;
    wire flush_calr = cur_valid && (x_state == `XS_DE) && is_calr;
    wire flush_ret  = cur_valid && (x_state == `XS_WB) && is_ret;
    wire flush = flush_jz || flush_jr || flush_calr || flush_ret;
    wire [15:0] flush_target = flush_ret ? {data_in_internal, ret_pc_lo} :
                                flush_jz ? jz_target : jmp_target;

    assign address = (is_ldm || is_stm) ? addr_base :
                      (is_calr || is_push || is_pop || is_ret) ? sp : 16'hxxxx;

    assign we = ((is_calr || is_stm || is_push) && (x_state == `XS_WB)) ||
                (is_calr && (x_state == `XS_MA));

    assign avma = (is_stm || is_ldm) && (x_state == `XS_MA || x_state == `XS_WB);

    wire [7:0] data_in_internal = (addr_latch == 16'h0000) ? sp[7:0] :
                                  (addr_latch == 16'h0001) ? sp[15:8] : data_in;

    // Does Execute finish *this* cycle (ready to accept a new instruction next cycle)?
    wire x_done = cur_valid && (
        ((x_state == `XS_DE) && !(is_stm || is_ldm || is_push || is_pop || is_calr || is_ret)) ||
        ((x_state == `XS_WB) && !((is_stm || is_ldm) && wait_req))
    );

    wire buf_consumed  = buf_valid && (!cur_valid || x_done);
    wire buf_will_fill = (!buf_valid || buf_consumed) && !flush;

    // ---------------------------------------------------------------
    // Fetch / Execute handoff, PC, SP, data_out, register writeback
    // ---------------------------------------------------------------
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            pc          <= 16'h0000;
            sp          <= 16'h0000;
            buf_valid   <= 1'b0;
            cur_valid   <= 1'b0;
            x_state     <= `XS_DE;
            carry_flag  <= 1'bx;
            addr_latch  <= 16'hxxxx;
            data_out    <= 8'hxx;
        end else begin
            // --- Execute stage advance ---
            if (!cur_valid || x_done) begin
                if (buf_valid && !flush) begin
                    cur_instr   <= buf_instr;
                    cur_pc_next <= buf_pc_next;
                    cur_valid   <= 1'b1;
                    x_state     <= `XS_DE;
                end else begin
                    cur_valid   <= 1'b0;
                end
            end else begin
                case (x_state)
                    `XS_DE: x_state <= `XS_MA;
                    `XS_MA: x_state <= `XS_WB;
                    `XS_WB: x_state <= `XS_WB; // only reached while stm/ldm honors wait_req
                    default: x_state <= `XS_DE;
                endcase
            end

            // --- Fetch stage advance ---
            if (flush) begin
                buf_valid <= 1'b0;
                pc        <= flush_target;
            end else if (buf_will_fill) begin
                buf_instr   <= rom[pc];
                buf_pc_next <= pc + 16'd1;
                buf_valid   <= 1'b1;
                pc          <= pc + 16'd1;
            end

            // --- Per-cycle execute-side effects (only meaningful while cur_valid) ---
            if (cur_valid) begin
                case (x_state)
                    `XS_DE: begin
                        if (is_x_mode || is_y_mode)
                            addr_base <= {ptr_hi_val, ptr_lo_val} + simm8;
                        else if (is_stm_n || is_ldm_n)
                            addr_base <= 16'h0000 + imm8;
                        else if (is_calr)
                            sp <= sp - 16'd1;
                        else if (is_ret)
                            sp <= sp + 16'd1;

                        if (is_alu || is_mvi) begin
                            regs[rd] <= rw_next;
                            if (is_alu) carry_flag <= carry_flag_next;
                        end

                        data_out <= is_calr ? cur_pc_next[15:8] : 8'hxx;
                    end
                    `XS_MA: begin
                        if (is_push || is_calr) sp <= sp - 16'd1;
                        if (is_pop  || is_ret)  sp <= sp + 16'd1;

                        if (is_ret) ret_pc_lo <= data_in_internal;

                        data_out <= is_push  ? rd_val :
                                    is_stm   ? rd_val :
                                    is_calr  ? cur_pc_next[7:0] : 8'hxx;
                    end
                    `XS_WB: begin
                        if (is_pop || is_ldm) regs[rd] <= rw_next;
                        // NOTE: is_ret's PC update is handled by the "Fetch stage advance"
                        // block above via flush/flush_target (flush_ret covers this case).
                        data_out <= 8'hxx;
                    end
                    default: ;
                endcase

                if (we && (address == 16'h0000)) sp[7:0]  <= data_out;
                else if (we && (address == 16'h0001)) sp[15:8] <= data_out;
                addr_latch <= address;
            end
        end
    end

    `ifdef SIM
    always_ff @(posedge clk) begin
        if (reset_n && cur_valid && (x_state == `XS_DE) && (instr == 18'h3FFFF)) begin
            $display("HALT encountered at PC=%h", cur_pc_next - 16'd1);
            $finish;
        end
    end
    `endif

endmodule
