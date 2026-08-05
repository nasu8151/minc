// minc CPU - classic 5-stage pipeline (IF/ID/EX/MEM/WB) with forwarding,
// binary compatible with minc_h.sv.
//
// Stage plan (see plan doc foamy-popping-treehouse.md):
//  - jz/jr/calr resolve in EX (operand/target need no memory) -> flush squashes
//    ifid+idex (2 bubbles).
//  - ret needs two sequential byte reads (low byte via MEM phase0->phase1, high byte
//    whose SSRAM response only lands the cycle ret is in WB) -> resolves in WB,
//    squashing ifid+idex+exmem (larger flush).
//  - stm/ldm/push/pop/calr/ret all occupy MEM for exactly 2 internal phases minimum
//    (phase0 = address/adjust setup, phase1 = the real bus transaction). Only
//    stm/ldm may extend phase1 further while wait_req is asserted; push/pop/calr/ret
//    never look at wait_req (matches minc_h.sv exactly).
//  - RAW hazards on regs[]/carry_flag/SP are forwarded from EX/MEM and MEM/WB into
//    EX, plus an ID-stage bypass of the pending WB write (covers the producer-3-
//    instructions-ahead case, where the WB write and the consumer's ID read land on
//    the same cycle). Load-use (ldm/pop consumed by the very next instruction) needs
//    one stall cycle since the loaded byte isn't ready anywhere forwardable yet.

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

    logic [15:0] pc;
    logic [15:0] sp;
    logic  [7:0] regs [0:15]; /* synthesis syn_ramstyle = "distributed" */
    logic        carry_flag;

    logic [17:0] rom [0:65535];
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    assign pc_out = pc;
    assign sp_out = sp;

    // ---------------------------------------------------------------
    // Decode helpers (pure functions of the 18-bit instruction word)
    // ---------------------------------------------------------------
    function automatic logic is_alu_f(logic [17:0] i);   is_alu_f   = (i[17:14] == 4'b0000); endfunction
    function automatic logic is_jz_f(logic [17:0] i);    is_jz_f    = (i[17:12] == 6'b001100); endfunction
    function automatic logic is_mvi_f(logic [17:0] i);   is_mvi_f   = (i[17:12] == 6'b001110); endfunction
    function automatic logic is_stmx_f(logic [17:0] i);  is_stmx_f  = (i[17:12] == 6'b010000); endfunction
    function automatic logic is_ldmx_f(logic [17:0] i);  is_ldmx_f  = (i[17:12] == 6'b010001); endfunction
    function automatic logic is_stmy_f(logic [17:0] i);  is_stmy_f  = (i[17:12] == 6'b010010); endfunction
    function automatic logic is_ldmy_f(logic [17:0] i);  is_ldmy_f  = (i[17:12] == 6'b010011); endfunction
    function automatic logic is_stmn_f(logic [17:0] i);  is_stmn_f  = (i[17:12] == 6'b010100); endfunction
    function automatic logic is_ldmn_f(logic [17:0] i);  is_ldmn_f  = (i[17:12] == 6'b010101); endfunction
    function automatic logic is_stm_f(logic [17:0] i);   is_stm_f   = (i[17:15] == 3'b010 && i[12] == 1'b0); endfunction
    function automatic logic is_ldm_f(logic [17:0] i);   is_ldm_f   = (i[17:15] == 3'b010 && i[12] == 1'b1); endfunction
    function automatic logic is_push_f(logic [17:0] i);  is_push_f  = (i[17:12] == 6'b011100); endfunction
    function automatic logic is_pop_f(logic [17:0] i);   is_pop_f   = (i[17:12] == 6'b011101); endfunction
    function automatic logic is_ret_f(logic [17:0] i);   is_ret_f   = (i[17:12] == 6'b011111); endfunction
    function automatic logic is_calr_f(logic [17:0] i);  is_calr_f  = (i[17:16] == 2'b10); endfunction
    function automatic logic is_jr_f(logic [17:0] i);    is_jr_f    = (i[17:16] == 2'b11); endfunction
    function automatic logic is_xmode_f(logic [17:0] i); is_xmode_f = is_stmx_f(i) || is_ldmx_f(i); endfunction
    function automatic logic is_ymode_f(logic [17:0] i); is_ymode_f = is_stmy_f(i) || is_ldmy_f(i); endfunction
    function automatic logic writes_gpr_f(logic [17:0] i);
        writes_gpr_f = is_alu_f(i) || is_mvi_f(i) || is_ldm_f(i) || is_pop_f(i);
    endfunction
    function automatic logic touches_sp_f(logic [17:0] i);
        touches_sp_f = is_push_f(i) || is_pop_f(i) || is_calr_f(i) || is_ret_f(i);
    endfunction
    function automatic logic touches_mem_f(logic [17:0] i);
        touches_mem_f = is_stm_f(i) || is_ldm_f(i) || touches_sp_f(i);
    endfunction
    function automatic logic [3:0] rd_f(logic [17:0] i); rd_f = i[7:4]; endfunction
    function automatic logic [3:0] rs_f(logic [17:0] i); rs_f = i[3:0]; endfunction
    function automatic logic [7:0] imm8_f(logic [17:0] i); imm8_f = {i[11:8], i[3:0]}; endfunction
    function automatic logic signed [15:0] simm8_f(logic [17:0] i);
        simm8_f = 16'($signed(imm8_f(i)));
    endfunction
    function automatic logic signed [15:0] simm16_f(logic [17:0] i);
        simm16_f = 16'($signed({i[15:12], i[7:4], i[11:8], i[3:0]}));
    endfunction

    // ---------------------------------------------------------------
    // Pipeline registers
    // ---------------------------------------------------------------
    logic        ifid_valid;
    logic [17:0] ifid_instr;
    logic [15:0] ifid_pcnext;

    logic        idex_valid;
    logic [17:0] idex_instr;
    logic [15:0] idex_pcnext;
    logic [7:0]  idex_rdv, idex_rsv, idex_phv, idex_plv;
    logic        idex_cflag;

    // EX/MEM - also the live MEM-stage working register; mem_phase tracks progress
    logic        exmem_valid;
    logic [17:0] exmem_instr;
    logic [15:0] exmem_pcnext;
    logic [7:0]  exmem_alu;
    logic        exmem_cflag_next;
    logic [15:0] exmem_addr;    // stm/ldm address (constant across phases)
    logic [7:0]  exmem_store0;  // byte currently queued to write (stm/push/calr)
    logic [15:0] exmem_sp;      // SP forward candidate / current phase's SP-derived address
    logic        exmem_memphase;

    logic        memwb_valid;
    logic [17:0] memwb_instr;
    logic [15:0] memwb_pcnext;
    logic [7:0]  memwb_aluval;  // value to commit to regs[] (alu/mvi passthrough, or captured load byte)
    logic        memwb_cflag;
    logic [15:0] memwb_sp;
    logic [7:0]  memwb_retlo;   // ret's captured low byte, high byte read fresh in WB

    // ---------------------------------------------------------------
    // ID-stage decode / raw register reads (with WB same-cycle bypass)
    // ---------------------------------------------------------------
    wire [17:0] fetched_instr = rom[pc];

    wire [3:0] ifid_rd = rd_f(ifid_instr);
    wire [3:0] ifid_rs = rs_f(ifid_instr);
    wire ifid_xmode = is_xmode_f(ifid_instr);
    wire ifid_ymode = is_ymode_f(ifid_instr);

    wire        wb_reg_we  = memwb_valid && writes_gpr_f(memwb_instr);
    wire [3:0]  wb_reg_idx = rd_f(memwb_instr);
    wire [7:0]  wb_reg_val = memwb_aluval;

    function automatic logic [7:0] id_bypass(logic [3:0] idx, logic [7:0] raw, logic we, logic [3:0] we_idx, logic [7:0] we_val);
        id_bypass = (we && we_idx == idx) ? we_val : raw;
    endfunction

    wire [7:0] id_rd_val = id_bypass(ifid_rd, regs[ifid_rd], wb_reg_we, wb_reg_idx, wb_reg_val);
    wire [7:0] id_rs_val = id_bypass(ifid_rs, regs[ifid_rs], wb_reg_we, wb_reg_idx, wb_reg_val);
    wire [7:0] id_ph_val = ifid_xmode ? id_bypass(4'd13, regs[4'd13], wb_reg_we, wb_reg_idx, wb_reg_val) :
                           ifid_ymode ? id_bypass(4'd15, regs[4'd15], wb_reg_we, wb_reg_idx, wb_reg_val) : 8'h00;
    wire [7:0] id_pl_val = ifid_xmode ? id_bypass(4'd12, regs[4'd12], wb_reg_we, wb_reg_idx, wb_reg_val) :
                           ifid_ymode ? id_bypass(4'd14, regs[4'd14], wb_reg_we, wb_reg_idx, wb_reg_val) : 8'h00;
    wire       id_cflag  = carry_flag;

    // ---------------------------------------------------------------
    // Load-use hazard detection (stall IF/ID/EX for one cycle)
    // ---------------------------------------------------------------
    wire idex_is_load = idex_valid && (is_ldm_f(idex_instr) || is_pop_f(idex_instr));
    wire [3:0] idex_dst = rd_f(idex_instr);
    wire ifid_reads_rd  = ifid_valid && (is_alu_f(ifid_instr) || is_jz_f(ifid_instr) || is_stm_f(ifid_instr));
    wire ifid_reads_rs  = ifid_valid && is_alu_f(ifid_instr);
    wire ifid_reads_ptr = ifid_valid && (ifid_xmode || ifid_ymode);
    wire load_use_hazard = idex_is_load && (
        (ifid_reads_rd  && (ifid_rd == idex_dst)) ||
        (ifid_reads_rs  && (ifid_rs == idex_dst)) ||
        (ifid_reads_ptr && ((idex_dst == 4'd12) || (idex_dst == 4'd13) || (idex_dst == 4'd14) || (idex_dst == 4'd15)))
    );

    // ---------------------------------------------------------------
    // EX-stage forwarding muxes
    // ---------------------------------------------------------------
    wire exmem_val_ready = exmem_valid && (is_alu_f(exmem_instr) || is_mvi_f(exmem_instr));
    wire [3:0] exmem_dst = rd_f(exmem_instr);
    wire memwb_val_ready = memwb_valid && writes_gpr_f(memwb_instr);
    wire [3:0] memwb_dst = rd_f(memwb_instr);

    function automatic logic [7:0] ex_fwd(logic [3:0] idx, logic [7:0] raw,
                                           logic em_rdy, logic [3:0] em_dst, logic [7:0] em_val,
                                           logic wb_rdy, logic [3:0] wb_dst, logic [7:0] wb_val);
        if (em_rdy && em_dst == idx)
            ex_fwd = em_val;
        else if (wb_rdy && wb_dst == idx)
            ex_fwd = wb_val;
        else
            ex_fwd = raw;
    endfunction

    wire [3:0] idex_rd = rd_f(idex_instr);
    wire [3:0] idex_rs = rs_f(idex_instr);
    wire idex_xmode = is_xmode_f(idex_instr);
    wire idex_ymode = is_ymode_f(idex_instr);

    wire [7:0] ex_rd_val = ex_fwd(idex_rd, idex_rdv, exmem_val_ready, exmem_dst, exmem_alu, memwb_val_ready, memwb_dst, memwb_aluval);
    wire [7:0] ex_rs_val = ex_fwd(idex_rs, idex_rsv, exmem_val_ready, exmem_dst, exmem_alu, memwb_val_ready, memwb_dst, memwb_aluval);
    wire [7:0] ex_ph_val = idex_xmode ? ex_fwd(4'd13, idex_phv, exmem_val_ready, exmem_dst, exmem_alu, memwb_val_ready, memwb_dst, memwb_aluval) :
                           idex_ymode ? ex_fwd(4'd15, idex_phv, exmem_val_ready, exmem_dst, exmem_alu, memwb_val_ready, memwb_dst, memwb_aluval) : 8'h00;
    wire [7:0] ex_pl_val = idex_xmode ? ex_fwd(4'd12, idex_plv, exmem_val_ready, exmem_dst, exmem_alu, memwb_val_ready, memwb_dst, memwb_aluval) :
                           idex_ymode ? ex_fwd(4'd14, idex_plv, exmem_val_ready, exmem_dst, exmem_alu, memwb_val_ready, memwb_dst, memwb_aluval) : 8'h00;

    wire exmem_cflag_ready = exmem_valid && is_alu_f(exmem_instr);
    wire memwb_cflag_ready = memwb_valid && is_alu_f(memwb_instr);
    wire ex_cflag = exmem_cflag_ready ? exmem_cflag_next :
                    memwb_cflag_ready ? memwb_cflag : idex_cflag;

    wire exmem_sp_ready = exmem_valid && touches_sp_f(exmem_instr);
    wire memwb_sp_ready  = memwb_valid && touches_sp_f(memwb_instr);
    wire [15:0] ex_sp = exmem_sp_ready ? exmem_sp : memwb_sp_ready ? memwb_sp : sp;

    // ---------------------------------------------------------------
    // EX-stage ALU / address / branch resolution
    // ---------------------------------------------------------------
    wire [3:0] idex_subop = idex_instr[13:10];
    logic [7:0] ex_alu_out;
    logic       ex_carry_next;
    always_comb begin
        case (idex_subop)
            4'b0000: begin ex_alu_out = ex_rs_val; ex_carry_next = 1'bx; end
            4'b0001: begin ex_alu_out = ex_rd_val | ex_rs_val; ex_carry_next = 1'bx; end
            4'b0010: begin ex_alu_out = ex_rd_val & ex_rs_val; ex_carry_next = 1'bx; end
            4'b0011: begin ex_alu_out = ex_rd_val ^ ex_rs_val; ex_carry_next = 1'bx; end
            4'b0100: {ex_carry_next, ex_alu_out} = ex_rd_val + ex_rs_val;
            4'b0101: {ex_carry_next, ex_alu_out} = ex_rd_val + ex_rs_val + ex_cflag;
            4'b0110: {ex_carry_next, ex_alu_out} = ex_rd_val + {1'b0, ~ex_rs_val} + 1'b1;
            4'b0111: {ex_carry_next, ex_alu_out} = ex_rd_val + {1'b0, ~ex_rs_val} + ex_cflag;
            4'b1000: {ex_carry_next, ex_alu_out} = (ex_rd_val < ex_rs_val) ? 8'b1 : 8'b0;
            4'b1001: {ex_carry_next, ex_alu_out} = (ex_rd_val < ex_rs_val - ex_cflag) ? 8'b1 : 8'b0;
            4'b1011: {ex_alu_out, ex_carry_next} = ex_rs_val >> 1 | (ex_cflag << 7);
            4'b1110: begin ex_alu_out = ex_rd_val * ex_rs_val; ex_carry_next = 1'bx; end
            4'b1111: begin ex_alu_out = (ex_rd_val * ex_rs_val) >> 8; ex_carry_next = 1'bx; end
            default: begin ex_alu_out = ex_rd_val; ex_carry_next = 1'bx; end
        endcase
    end

    wire [15:0] ex_addr_base = (idex_xmode || idex_ymode) ? ({ex_ph_val, ex_pl_val} + simm8_f(idex_instr)) :
                               (is_stmn_f(idex_instr) || is_ldmn_f(idex_instr)) ? (16'h0000 + imm8_f(idex_instr)) :
                               16'hxxxx;

    wire ex_jz_taken  = idex_valid && is_jz_f(idex_instr) && (ex_rd_val == 8'd0);
    // Gated by mem_advance: while idex is stalled behind a busy MEM stage, this must
    // NOT fire repeatedly - otherwise the branch/calr itself gets squashed out of idex
    // before it ever advances into EX/MEM (calr's SP-decrement-and-push would be lost).
    wire flush_ex     = idex_valid && mem_advance && (ex_jz_taken || is_jr_f(idex_instr) || is_calr_f(idex_instr));
    wire [15:0] flush_ex_target = is_jz_f(idex_instr) ? (idex_pcnext + simm8_f(idex_instr))
                                                       : (idex_pcnext + simm16_f(idex_instr));

    // Only calr adjusts SP in EX (first of its two decrements); push/pop/ret adjust entirely in MEM.
    wire [15:0] ex_sp_after = is_calr_f(idex_instr) ? (ex_sp - 16'd1) : ex_sp;

    // ---------------------------------------------------------------
    // MEM-stage combinational outputs
    // ---------------------------------------------------------------
    wire mm_is_stm  = is_stm_f(exmem_instr);
    wire mm_is_ldm  = is_ldm_f(exmem_instr);
    wire mm_is_push = is_push_f(exmem_instr);
    wire mm_is_pop  = is_pop_f(exmem_instr);
    wire mm_is_calr = is_calr_f(exmem_instr);
    wire mm_is_ret  = is_ret_f(exmem_instr);
    wire mm_touches_mem = touches_mem_f(exmem_instr);
    wire mm_can_wait    = mm_is_stm || mm_is_ldm;

    assign address = !exmem_valid ? 16'hxxxx :
                     (mm_is_stm || mm_is_ldm) ? exmem_addr :
                     (mm_is_push || mm_is_pop || mm_is_calr || mm_is_ret) ? exmem_sp : 16'hxxxx;

    assign we = exmem_valid && (
                    ((mm_is_stm || mm_is_push) && exmem_memphase) ||
                    mm_is_calr
                );

    assign avma = exmem_valid && (mm_is_stm || mm_is_ldm);

    logic [15:0] mem_addr_latch;
    wire [7:0] data_in_internal = (mem_addr_latch == 16'h0000) ? sp[7:0] :
                                  (mem_addr_latch == 16'h0001) ? sp[15:8] : data_in;

    assign data_out = !exmem_valid ? 8'hxx :
                      (mm_is_stm || mm_is_push || mm_is_calr) ? exmem_store0 : 8'hxx;

    // mem_done: instruction is ready to leave MEM for WB this cycle.
    wire mem_done = exmem_valid && (
                        !mm_touches_mem ? 1'b1 :
                        (exmem_memphase && !(mm_can_wait && wait_req))
                    );
    wire mem_busy = exmem_valid && !mem_done;

    // ---------------------------------------------------------------
    // ret resolves in WB: memwb_retlo (low byte, captured leaving MEM) + fresh
    // data_in_internal this cycle (high byte, SSRAM's 1-cycle-delayed reply to
    // exmem's phase1 address) together form the popped PC.
    // ---------------------------------------------------------------
    wire flush_wb = memwb_valid && is_ret_f(memwb_instr);
    wire [15:0] flush_wb_target = {data_in_internal, memwb_retlo};

    // ---------------------------------------------------------------
    // Stall control
    // ---------------------------------------------------------------
    wire stall_for_mem  = mem_busy;
    wire stall_for_load = load_use_hazard && !stall_for_mem;
    wire advance = !stall_for_mem && !stall_for_load; // gates IF/ID and the ID->ID/EX load
    wire mem_advance = !stall_for_mem; // gates ID/EX->EX/MEM: a load-use stall must NOT
                                        // block the load itself, only the consumer behind it

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            pc <= 16'h0000;
            sp <= 16'h0000;
            carry_flag <= 1'bx;
            ifid_valid  <= 1'b0;
            idex_valid  <= 1'b0;
            exmem_valid <= 1'b0;
            memwb_valid <= 1'b0;
            exmem_memphase <= 1'b0;
            mem_addr_latch <= 16'hxxxx;
        end else begin
            // ---------------- IF ----------------
            if (flush_wb)       pc <= flush_wb_target;
            else if (flush_ex)  pc <= flush_ex_target;
            else if (advance)   pc <= pc + 16'd1;

            if (flush_wb || flush_ex) ifid_valid <= 1'b0;
            else if (advance) begin
                ifid_valid  <= 1'b1;
                ifid_instr  <= fetched_instr;
                ifid_pcnext <= pc + 16'd1;
            end
            // else: hold (stalled)

            // ---------------- ID -> ID/EX ----------------
            if (flush_wb) begin
                idex_valid <= 1'b0;
            end else if (flush_ex) begin
                idex_valid <= 1'b0; // ifid content (sequential successor) is also wrong-path
            end else if (stall_for_load) begin
                idex_valid <= 1'b0; // bubble injected into EX
            end else if (advance) begin
                idex_valid  <= ifid_valid;
                idex_instr  <= ifid_instr;
                idex_pcnext <= ifid_pcnext;
                idex_rdv    <= id_rd_val;
                idex_rsv    <= id_rs_val;
                idex_phv    <= id_ph_val;
                idex_plv    <= id_pl_val;
                idex_cflag  <= id_cflag;
            end
            // else: hold (stalled behind MEM)

            // ---------------- EX -> EX/MEM, and MEM-phase progression ----------------
            if (flush_wb) begin
                // idex/exmem content (1 and 2 ahead of the retiring ret) is wrong-path too;
                // squash unconditionally, even if MEM would otherwise be mid-phase.
                exmem_valid <= 1'b0;
            end else if (!stall_for_mem) begin
                // MEM is free this cycle (its previous occupant, if any, drains to memwb below).
                // Uses mem_advance (not advance): a load-use stall must not hold back the
                // load itself, only the ifid->idex transfer for the instruction behind it.
                if (mem_advance && idex_valid) begin
                    exmem_valid      <= 1'b1;
                    exmem_instr      <= idex_instr;
                    exmem_pcnext     <= idex_pcnext;
                    exmem_alu        <= is_mvi_f(idex_instr) ? imm8_f(idex_instr) : ex_alu_out;
                    exmem_cflag_next <= ex_carry_next;
                    exmem_addr       <= ex_addr_base;
                    exmem_store0     <= is_calr_f(idex_instr) ? idex_pcnext[15:8] : ex_rd_val;
                    exmem_sp         <= ex_sp_after;
                    exmem_memphase   <= 1'b0;
                end else begin
                    // Either idex has nothing valid, or a load-use stall (advance=0) holds
                    // idex in place for next cycle - either way exmem must not keep stale data.
                    exmem_valid <= 1'b0;
                end
            end else begin
                // MEM busy: progress its internal phase (idex/ifid/pc held above)
                if (!exmem_memphase) begin
                    exmem_memphase <= 1'b1;
                    if (mm_is_calr) begin
                        exmem_sp     <= exmem_sp - 16'd1;
                        exmem_store0 <= exmem_pcnext[7:0];
                    end else if (mm_is_push) begin
                        exmem_sp <= exmem_sp - 16'd1;
                    end else if (mm_is_pop || mm_is_ret) begin
                        exmem_sp <= exmem_sp + 16'd1;
                    end
                end
                // else: already in phase1, looping on wait_req (stm/ldm only) - nothing to update
            end

            mem_addr_latch <= address;
            if (we && address == 16'h0000) sp[7:0]  <= data_out;
            else if (we && address == 16'h0001) sp[15:8] <= data_out;

            // ---------------- MEM -> MEM/WB ----------------
            if (flush_wb) begin
                memwb_valid <= 1'b0; // exmem's occupant this cycle is also wrong-path; don't let it retire
            end else if (!stall_for_mem) begin
                if (exmem_valid && mem_done) begin
                    memwb_valid  <= 1'b1;
                    memwb_instr  <= exmem_instr;
                    memwb_pcnext <= exmem_pcnext;
                    memwb_aluval <= (mm_is_ldm || mm_is_pop) ? data_in_internal : exmem_alu;
                    memwb_cflag  <= exmem_cflag_next;
                    memwb_sp     <= mm_is_ret ? (exmem_sp + 16'd1) : exmem_sp;
                    memwb_retlo  <= mm_is_ret ? data_in_internal : 8'hxx;
                end else begin
                    memwb_valid <= 1'b0;
                end
            end

            // ---------------- WB (commit) ----------------
            if (memwb_valid) begin
                if (writes_gpr_f(memwb_instr))
                    regs[rd_f(memwb_instr)] <= memwb_aluval;
                if (is_alu_f(memwb_instr))
                    carry_flag <= memwb_cflag;
                if (touches_sp_f(memwb_instr))
                    sp <= memwb_sp;
            end

            `ifdef SIM
            // Wait until HALT reaches WB (not just EX) so earlier in-flight
            // instructions still draining through MEM/WB get to commit first.
            if (memwb_valid && (memwb_instr == 18'h3FFFF)) begin
                $display("HALT encountered at PC=%h", memwb_pcnext - 16'd1);
                $finish;
            end
            `endif
        end
    end

endmodule
