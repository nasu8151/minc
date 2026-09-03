#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_16.h"
#include "errorhandle.h"

/* ------------------------------------------------------------------ *
 * Register map -- see the ABI comment in codegen_16.h
 * ------------------------------------------------------------------ */
enum {
    R_SCRATCH = 0,  // return value, and the address scratch for out-of-range operands
    AST_MIN   = 1,  // bottom of the expression stack (also where arg 0 is passed)
    CALLER_MAX = 5, // r1-r5 are caller-saved
    AST_MAX   = 13, // r14/r15 are BP/SP: the expression stack must never reach them
    R_BP      = 14,
    R_SP      = 15,
};

// `[rB+n]` carries a signed 6-bit displacement; `addi`/`mvi` carry a sign-extended
// 8-bit immediate. Anything wider has to be materialised through R_SCRATCH.
#define DISP_MIN (-32)
#define DISP_MAX (31)
#define SIMM8_MIN (-128)
#define SIMM8_MAX (127)

static int  nxt_regstack_top;
static int  cur_regstack_max;
static int  cur_return_size;
static bool cur_is_isr;

// All generated assembly goes through emit(). While g_mute is set the output is
// discarded, which is what lets ND_FUNC_DEF run a function body once as a dry
// run to measure how far up the regstack it climbs before writing anything.
static int g_mute;

static void emit(const char *fmt, ...) {
    if (g_mute) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static int generate(Node *node, int expected_size);

/* ------------------------------------------------------------------ *
 * Regstack
 * ------------------------------------------------------------------ */
// Every C value minc-16 can hold -- char, int, any pointer -- fits in one 16-bit
// register, so a slot is always exactly one register and `size` never changes how
// far the stack moves. It only picks the memory access width (ldb/stb vs ldw/stw).
//
// push_regstack only records how far up the register file the function reaches;
// it emits nothing. The matching push/pop pair is written once by the
// prologue/epilogue (see ND_FUNC_DEF).
static int push_regstack(void) {
    int cur = nxt_regstack_top;
    if (cur > AST_MAX) {
        // r14/r15 are BP/SP; spilling the expression stack into them would
        // silently destroy the frame, so refuse instead.
        error("Expression too deep for the minc-16 register stack (needs r%d, max r%d)",
              cur, AST_MAX);
    }
    if (cur_regstack_max < cur) {
        cur_regstack_max = cur;
    }
    nxt_regstack_top = cur + 1;
    return cur;
}

static int pop_regstack(void) {
    nxt_regstack_top -= 1;
    return nxt_regstack_top;
}

// Index of the topmost live slot, without moving the stack.
static int top_regstack(void) {
    return nxt_regstack_top - 1;
}

static int set_regstack(int value) {
    int before = nxt_regstack_top;
    nxt_regstack_top = value;
    return before;
}

/* ------------------------------------------------------------------ *
 * Labels
 * ------------------------------------------------------------------ */
static unsigned long label_id = 0;

static char *get_unique_label(void) {
    char *label = calloc(24, sizeof(char));
    if (!label) {
        error("Memory allocation failed");
    }
    sprintf(label, "__L%lu", ++label_id);
    return label;
}

// Innermost-loop end labels, for `break`. minc-8 keeps a single `current_end`
// global, which an inner loop overwrites -- so a `break` sitting after a nested
// loop jumped to the *inner* loop's end. A stack costs nothing and cannot get
// that wrong.
#define BREAK_DEPTH_MAX 32
static char *break_labels[BREAK_DEPTH_MAX];
static int   break_depth;

static void push_break_label(char *label) {
    if (break_depth >= BREAK_DEPTH_MAX) {
        error("Loop nesting too deep (max %d)", BREAK_DEPTH_MAX);
    }
    break_labels[break_depth++] = label;
}

static void pop_break_label(void) {
    if (break_depth > 0) {
        break_depth--;
    }
}

// ND_FUNC_DEF generates each body twice (see there). Rewinding the label counter
// after the discarded pass keeps the labels that actually get emitted contiguous.
static unsigned long label_mark(void) {
    return label_id;
}

static void label_rewind(unsigned long mark) {
    label_id = mark;
}

/* ------------------------------------------------------------------ *
 * Immediate / address helpers
 * ------------------------------------------------------------------ */
// `mvi rd,n` sign-extends its 8-bit immediate, so the high byte only needs a
// separate `mvih` when it differs from that sign extension. Constants that happen
// to be small (or 0xFFxx) therefore cost one instruction instead of two.
static void emit_load_const(int reg, long value) {
    long w  = value & 0xFFFF;
    long lo = w & 0xFF;
    long hi = (w >> 8) & 0xFF;
    emit("mvi r%d,%ld\n", reg, lo);
    if (hi != ((lo & 0x80) ? 0xFF : 0x00)) {
        emit("mvih r%d,%ld\n", reg, hi);
    }
}

// reg += value. `addi` covers a sign-extended 8-bit delta; anything wider goes
// through the scratch register (which must not be `reg` itself).
static void emit_add_const(int reg, long value) {
    long v = (long)(short)(value & 0xFFFF);
    if (v == 0) {
        return;
    }
    if (v >= SIMM8_MIN && v <= SIMM8_MAX) {
        emit("addi r%d,%ld\n", reg, v);
        return;
    }
    emit_load_const(R_SCRATCH, v);
    emit("add r%d,r%d\n", reg, R_SCRATCH);
}

static const char *mem_mnemonic(bool is_load, int size) {
    if (size == 1) {
        return is_load ? "ldb" : "stb";
    }
    return is_load ? "ldw" : "stw";
}

static void check_mem_size(int size, char *loc) {
    if (size != 1 && size != 2) {
        error_at(loc, "Invalid size for a memory access: %d", size);
    }
}

// Load/store `reg` through (base + off) using the base+displacement form. A
// displacement that does not fit the 6-bit field is folded into R_SCRATCH first;
// the constant is loaded there and the base added to it, so only one scratch
// register is ever needed.
static void emit_mem_disp(bool is_load, int size, int reg, int base, long off, char *loc) {
    check_mem_size(size, loc);
    int b = base;
    long d = (long)(short)(off & 0xFFFF);
    if (d < DISP_MIN || d > DISP_MAX) {
        emit_load_const(R_SCRATCH, d);
        emit("add r%d,r%d\n", R_SCRATCH, b);
        b = R_SCRATCH;
        d = 0;
    }
    if (is_load) {
        emit("%s r%d,[r%d%+ld]\n", mem_mnemonic(is_load, size), reg, b, d);
    } else {
        emit("%s [r%d%+ld],r%d\n", mem_mnemonic(is_load, size), b, d, reg);
    }
}

// Load/store `reg` at an absolute data address. The 8-bit absolute form only
// reaches 0x00-0xFF, and a word access there ignores address bit 0, so anything
// else (every auto-allocated global -- those start at 0x100) goes through a
// materialised pointer instead.
static void emit_mem_abs(bool is_load, int size, int reg, long addr, char *loc) {
    check_mem_size(size, loc);
    long a = addr & 0xFFFF;
    if (a <= 0xFF && (size == 1 || (a & 1) == 0)) {
        if (is_load) {
            emit("%s r%d,%ld\n", mem_mnemonic(is_load, size), reg, a);
        } else {
            emit("%s %ld,r%d\n", mem_mnemonic(is_load, size), a, reg);
        }
        return;
    }
    emit_load_const(R_SCRATCH, a);
    emit_mem_disp(is_load, size, reg, R_SCRATCH, 0, loc);
}

/* ------------------------------------------------------------------ *
 * Prologue / epilogue
 * ------------------------------------------------------------------ */
// Lowest register index this function is responsible for preserving across a
// call. r1-r5 are caller-saved (see ND_FUNC_CALL), but an ISR has no software
// caller to have done that, so it owns everything the regstack machine touches.
// r0/r1 are saved unconditionally by the ISR prologue (sei()/cli() and inline asm
// clobber both before any reactive protection could apply), hence the +1 here.
// Both the prologue's pushes and the epilogue's pops read this, so they cannot
// disagree about how many words are on the stack.
static int callee_save_lo(void) {
    return cur_is_isr ? AST_MIN + 1 : CALLER_MAX + 1;
}

// Save/restore of the callee-owned registers, r<callee_save_lo()>..r<high>. Both
// sides derive their range from the same pair of values, so they cannot disagree
// about how many words are on the stack. The saves sit *above* the frame-pointer
// load in the prologue, which leaves BP pointing just below them -- hence the
// epilogue restores SP from BP before popping, and the body is free to leave SP
// wherever it likes.
static void emit_callee_saves(int reg_high_water) {
    for (int r = callee_save_lo(); r <= reg_high_water; r++) {
        emit("push r%d\n", r);
    }
}

static void emit_callee_restores(int reg_high_water) {
    for (int r = reg_high_water; r >= callee_save_lo(); r--) {
        emit("pop r%d\n", r);
    }
}

// push/pop move SP by 2 and 16-bit accesses ignore address bit 0, so the frame
// has to stay word-aligned. nodes.c already pads every slot to an even size under
// -16; this is the belt-and-braces half of that invariant.
static long align_frame(long bytes) {
    if (bytes < 0) {
        bytes = 0;
    }
    return (bytes + 1) & ~1L;
}

static void generate_prologue(Node **args, long local_var_bytes, int reg_high_water) {
    set_regstack(AST_MIN);
    emit("push r%d\n", R_BP);
    emit_callee_saves(reg_high_water);
    emit("mov r%d,r%d\n", R_BP, R_SP);
    emit_add_const(R_SP, -align_frame(local_var_bytes));

    // Arguments arrive one per register from AST_MIN up (a 16-bit register holds
    // any of them), and each parameter node already carries the frame offset the
    // parser assigned it -- so spill straight to that, rather than recomputing the
    // layout here and risking the two disagreeing. This runs *after* SP has been
    // lowered: writing below SP would leave the spilled arguments inside the
    // window an interrupt entry pushes into.
    if (args) {
        int reg = AST_MIN;
        for (Node **arg = args; *arg; arg++) {
            Node *a = *arg;
            int sz = (a->valtype) ? a->valtype->size : 2;
            emit_mem_disp(false, sz, reg, R_BP, a->ofs_addr, a->loc);
            reg++;
        }
    }
}

static void generate_epilogue(int size, char *loc) {
    // 戻り値を先に r0 へ退避してから復元する。復元は callee 責任のレジスタを
    // 上書きするので、戻り値がそこに乗っている場合に壊れるため。
    if (size == 1 || size == 2) {
        emit("mov r%d,r%d\n", R_SCRATCH, pop_regstack());
    } else if (size != 0) {
        error_at(loc, "Invalid size for return value: %d", size);
    }
    emit("mov r%d,r%d\n", R_SP, R_BP); // SP = BP : 退避したレジスタの直上まで巻き戻す
    emit_callee_restores(cur_regstack_max);
    emit("pop r%d\n", R_BP);
    emit("ret\n");
}

// ISR prologue/epilogue: no arguments, no return value (validated at parse time),
// r0/r1 saved unconditionally, and reti instead of ret so PSR is restored from
// PSR_SHADOW (re-enabling IE). minc-8 additionally bracketed the X pointer
// (r12:r13) here; minc-16 needs no such thing because a pointer fits in one
// register and the only implicit scratch is r0, saved right below.
static void generate_isr_prologue(long local_var_bytes, int reg_high_water) {
    set_regstack(AST_MIN);
    emit("push r%d\n", R_SCRATCH);
    emit("push r%d\n", AST_MIN);
    emit("push r%d\n", R_BP);
    emit_callee_saves(reg_high_water);
    emit("mov r%d,r%d\n", R_BP, R_SP);
    emit_add_const(R_SP, -align_frame(local_var_bytes));
}

static void generate_isr_epilogue(void) {
    emit("mov r%d,r%d\n", R_SP, R_BP);
    emit_callee_restores(cur_regstack_max);
    emit("pop r%d\n", R_BP);
    emit("pop r%d\n", AST_MIN);
    emit("pop r%d\n", R_SCRATCH);
    emit("reti\n");
}

/* ------------------------------------------------------------------ *
 * Operators
 * ------------------------------------------------------------------ */
// Both operands are already on the regstack (the right one on top). minc-8 needed
// an 8-bit and a 16-bit lowering for every operator because a 16-bit value lived
// in a register pair; here one 16-bit instruction covers both widths.
static int gen_binop(Node *node) {
    int src = (node->type == ND_NOT) ? top_regstack() : pop_regstack();
    int dst = top_regstack();
    int result_size = 2;

    switch (node->type) {
    case ND_ADD:
        emit("add r%d,r%d\n", dst, src);
        break;
    case ND_SUB:
        emit("sub r%d,r%d\n", dst, src);
        break;
    case ND_MUL:
        emit("mul r%d,r%d\n", dst, src);
        break;
    case ND_BITWISE_AND:
        emit("and r%d,r%d\n", dst, src);
        break;
    case ND_BITWISE_OR:
        emit("or r%d,r%d\n", dst, src);
        break;
    case ND_BITWISE_XOR:
        emit("xor r%d,r%d\n", dst, src);
        break;
    // chz reads its *destination* on minc-16 (rd = (rd == 0)), unlike minc-8
    // where it reads rs -- see the instruction tables in Hardware.md.
    case ND_EQ:
        emit("sub r%d,r%d\nchz r%d,r%d\n", dst, src, dst, dst);
        result_size = 1;
        break;
    case ND_NEQ: // chz twice = "is not zero"
        emit("sub r%d,r%d\nchz r%d,r%d\nchz r%d,r%d\n", dst, src, dst, dst, dst, dst);
        result_size = 1;
        break;
    case ND_LT: // unsigned, as on minc-8
        emit("lt r%d,r%d\n", dst, src);
        result_size = 1;
        break;
    case ND_GE:
        emit("lt r%d,r%d\nchz r%d,r%d\n", dst, src, dst, dst);
        result_size = 1;
        break;
    case ND_AND: // (d != 0) && (s != 0) == !((d == 0) | (s == 0))
        emit("chz r%d,r%d\nchz r%d,r%d\nor r%d,r%d\nchz r%d,r%d\n",
             dst, dst, src, src, dst, src, dst, dst);
        result_size = 1;
        break;
    case ND_OR: // (d != 0) || (s != 0) == !((d == 0) & (s == 0))
        emit("chz r%d,r%d\nchz r%d,r%d\nand r%d,r%d\nchz r%d,r%d\n",
             dst, dst, src, src, dst, src, dst, dst);
        result_size = 1;
        break;
    case ND_NOT:
        emit("chz r%d,r%d\n", dst, dst);
        result_size = 1;
        break;
    default:
        error_at(node->loc, "Unknown node type: %d", node->type);
        break;
    }
    return result_size;
}

// Branch on a condition already evaluated onto the regstack. minc-16 has jz/jnz
// on a register, so the chz+jz pair minc-8 needed collapses to one instruction.
static void gen_branch_if_zero(Node *cond, const char *label) {
    generate(cond, NO_EXPECTED_SIZE);
    emit("jz r%d,%s\n", pop_regstack(), label);
}

/* ------------------------------------------------------------------ *
 * Statement / expression generation
 * ------------------------------------------------------------------ */
static int generate(Node *node, int expected_size) {
    if (!node) {
        return expected_size;
    }
    switch (node->type) {
    case ND_NUM: {
        int size = (expected_size == NO_EXPECTED_SIZE)
                 ? (node->valtype ? node->valtype->size : 2)
                 : expected_size;
        if (node->val < -32768 || node->val > 65535) {
            error_at(node->loc, "Value out of range for a 16-bit register: %ld", node->val);
        }
        emit_load_const(push_regstack(), node->val);
        return size;
    }
    case ND_LOCAL_VAR: {
        int actual = node->valtype->size;
        emit_mem_disp(true, actual, push_regstack(), R_BP, node->ofs_addr, node->loc);
        return actual;
    }
    case ND_GLOBAL_VAR: {
        int actual = node->valtype->size;
        emit_mem_abs(true, actual, push_regstack(), node->ofs_addr, node->loc);
        return actual;
    }
    case ND_ASSIGN: {
        int val_size = node->lhs->valtype->size;
        // `*p = e` needs the destination address pushed *below* the value, since
        // the ND_DEREF arm pops the value first. Emitting it here (rather than in
        // that arm) keeps the right-hand side evaluated exactly once for every
        // kind of destination.
        if (node->lhs->type == ND_DEREF) {
            generate(node->lhs->lhs, PTR_SIZE);
        }
        // A void right-hand side (asm(...), sei()/cli(), a void function call)
        // pushes nothing onto the regstack, so the pops below would unbalance it.
        if (generate(node->rhs, val_size) == 0) {
            error_at(node->loc, "Cannot assign a value of size 0 (void)");
        }
        if (node->lhs->type == ND_LOCAL_VAR) {
            emit_mem_disp(false, val_size, pop_regstack(), R_BP, node->lhs->ofs_addr, node->loc);
        } else if (node->lhs->type == ND_GLOBAL_VAR) {
            emit_mem_abs(false, val_size, pop_regstack(), node->lhs->ofs_addr, node->loc);
        } else if (node->lhs->type == ND_DEREF) {
            // 右辺値（代入すべき値）と左辺値（アドレス）は既に上で積んである
            int val = pop_regstack();
            int addr = pop_regstack();
            emit_mem_disp(false, val_size, val, addr, 0, node->loc);
        } else {
            error_at(node->loc, "Left-hand side of assignment must be a variable");
        }
        return val_size;
    }
    case ND_RETURN: {
        if (cur_is_isr && node->lhs) {
            error_at(node->loc, "ISR functions cannot return a value");
        }
        if (node->lhs) {
            generate(node->lhs, cur_return_size);
        }
        if (cur_is_isr) {
            generate_isr_epilogue();
        } else {
            generate_epilogue(cur_return_size, node->loc);
        }
        return cur_return_size;
    }
    case ND_IF: {
        char *end_label = get_unique_label();
        gen_branch_if_zero(node->cond, end_label);
        generate(node->lhs, NO_EXPECTED_SIZE); // then節
        emit("%s:\n", end_label);
        free(end_label);
        return 0;
    }
    case ND_IF_ELSE: {
        char *else_label = get_unique_label();
        char *end_label = get_unique_label();
        gen_branch_if_zero(node->cond, else_label);
        generate(node->lhs, NO_EXPECTED_SIZE); // then節
        emit("jr %s\n", end_label);
        emit("%s:\n", else_label);
        generate(node->else_, NO_EXPECTED_SIZE); // else節
        emit("%s:\n", end_label);
        free(else_label);
        free(end_label);
        return 0;
    }
    case ND_FOR: {
        if (node->init) {
            generate(node->init, NO_EXPECTED_SIZE);
        }
        char *begin_label = get_unique_label();
        char *end_label = get_unique_label();
        push_break_label(end_label);
        emit("%s:\n", begin_label);
        if (node->cond) {
            gen_branch_if_zero(node->cond, end_label);
        }
        generate(node->lhs, NO_EXPECTED_SIZE); // body
        if (node->inc) {
            generate(node->inc, NO_EXPECTED_SIZE);
        }
        emit("jr %s\n", begin_label);
        emit("%s:\n", end_label);
        pop_break_label();
        free(begin_label);
        free(end_label);
        return 0;
    }
    case ND_WHILE: {
        char *begin_label = get_unique_label();
        char *end_label = get_unique_label();
        push_break_label(end_label);
        emit("%s:\n", begin_label);
        gen_branch_if_zero(node->cond, end_label);
        generate(node->lhs, NO_EXPECTED_SIZE); // body
        emit("jr %s\n", begin_label);
        emit("%s:\n", end_label);
        pop_break_label();
        free(begin_label);
        free(end_label);
        return 0;
    }
    case ND_BREAK: {
        if (break_depth == 0) {
            error_at(node->loc, "`break` outside of a loop");
        }
        emit("jr %s\n", break_labels[break_depth - 1]);
        return 0;
    }
    case ND_BLOCK: {
        for (Node **member = node->body; member && *member; member++) {
            generate(*member, NO_EXPECTED_SIZE);
        }
        return 0;
    }
    case ND_FUNC_DEF: {
        cur_return_size = node->valtype->size;
        cur_is_isr = (node->valtype->type == TY_ISR);
        if (node->lhs->type != ND_BLOCK) {
            error_at(node->loc, "Function body must be a block");
        }

        // The callee-saved pushes belong in the prologue, where they run exactly
        // once per call -- but which registers the body needs is only known after
        // the body has been generated, and the prologue is written first. So the
        // body is generated twice: once with the output discarded, purely to
        // measure the high-water mark, then again for real with that number in
        // hand. Seeding cur_regstack_max before the second pass is also what
        // makes every `return` inside the body agree with the prologue, since
        // each one emits its own copy of the epilogue.
        unsigned long mark = label_mark();
        g_mute++;
        cur_regstack_max = AST_MIN - 1; // "nothing touched yet"
        if (cur_is_isr) {
            generate_isr_prologue(node->lhs->arg_sf_size, AST_MIN - 1);
        } else {
            generate_prologue(node->body, node->lhs->arg_sf_size, AST_MIN - 1);
        }
        generate(node->lhs, NO_EXPECTED_SIZE);
        int reg_high_water = cur_regstack_max;
        g_mute--;
        label_rewind(mark); // keep the labels that actually get emitted contiguous

        emit("%s:\n", node->name);
        if (cur_is_isr) {
            generate_isr_prologue(node->lhs->arg_sf_size, reg_high_water);
        } else {
            generate_prologue(node->body, node->lhs->arg_sf_size, reg_high_water);
        }
        cur_regstack_max = reg_high_water; // every epilogue below pops this exact range
        generate(node->lhs, NO_EXPECTED_SIZE); // function body
        if (cur_is_isr) {
            generate_isr_epilogue();
        } else {
            generate_epilogue(cur_return_size, node->loc);
        }
        cur_is_isr = false;
        return cur_return_size;
    }
    case ND_FUNC_CALL: {
        // Save exactly the caller-saved registers that are *live*: r<AST_MIN> up
        // to the caller's current top. Slots at or above it are dead, so letting
        // argument evaluation scribble on them is free; anything above CALLER_MAX
        // is callee-saved and the callee protects it. All of this has to happen
        // before the first argument is evaluated -- minc-8 pushed each argument
        // register just before the argument that lands in it, by which point an
        // earlier argument's evaluation had already clobbered it.
        int before = nxt_regstack_top;
        int save_hi = (before - 1 < CALLER_MAX) ? before - 1 : CALLER_MAX;
        for (int r = AST_MIN; r <= save_hi; r++) {
            emit("push r%d\n", r);
        }

        // Arguments go in r<AST_MIN>, r<AST_MIN>+1, ... -- one register each,
        // whatever their C type. Evaluating argument k+1 only ever touches
        // registers above the ones already holding arguments 0..k, so they stay
        // put until the call.
        set_regstack(AST_MIN);
        for (Node **arg = node->body; arg && *arg; arg++) {
            Node *a = *arg;
            int arg_size = (a->valtype) ? a->valtype->size : 2;
            if (generate(a, arg_size) == 0) {
                error_at(a->loc, "Cannot pass a value of size 0 (void)");
            }
        }
        emit("calr %s\n", node->name);

        for (int r = save_hi; r >= AST_MIN; r--) {
            emit("pop r%d\n", r);
        }
        set_regstack(before);

        int ret_size = (node->valtype) ? node->valtype->size : 2;
        if (ret_size == 1 || ret_size == 2) {
            emit("mov r%d,r%d\n", push_regstack(), R_SCRATCH);
        } else if (ret_size != 0) {
            error_at(node->loc, "Invalid return size: %d", ret_size);
        }
        return ret_size;
    }
    case ND_ADDR: {
        Node *target = node->lhs;
        int dst = push_regstack();
        if (target->type == ND_LOCAL_VAR) {
            emit("mov r%d,r%d\n", dst, R_BP);
            emit_add_const(dst, target->ofs_addr);
        } else if (target->type == ND_GLOBAL_VAR) {
            emit_load_const(dst, target->ofs_addr);
        } else {
            error_at(node->loc, "Cannot take the address of this expression");
        }
        return PTR_SIZE;
    }
    case ND_DEREF: {
        generate(node->lhs, PTR_SIZE);
        int ptr = pop_regstack();
        int dst = push_regstack(); // same slot: the pointer is consumed in place
        emit_mem_disp(true, node->valtype->size, dst, ptr, 0, node->loc);
        return node->valtype->size;
    }
    case ND_ASM: {
        // Inline assembly is passed straight through to mincasm. The regstack is
        // deliberately left untouched: preserving whatever registers the block
        // clobbers is the author's responsibility (r0/r1 are always free here).
        emit("%s", node->name);
        if (node->name_len == 0 || node->name[node->name_len - 1] != '\n') {
            emit("\n"); // mincasm is line-oriented, so never merge with the next instruction
        }
        return 0;
    }
    default:
        break;
    }

    // Binary/unary operators. Unlike minc-8 there is nothing to reconcile between
    // the operands: both are already full 16-bit registers, so a char operand is
    // simply promoted (C's usual arithmetic conversions) and no cast code is
    // needed in either direction.
    int lhs_size = generate(node->lhs, NO_EXPECTED_SIZE);
    int rhs_size = lhs_size;
    if (node->rhs) {
        rhs_size = generate(node->rhs, NO_EXPECTED_SIZE);
    }
    if (lhs_size == 0 || rhs_size == 0) {
        error_at(node->loc, "Cannot use a value of size 0 (void) in an expression");
    }

    int result_size = gen_binop(node);
    // A comparison yields a 0/1 flag (size 1); everything else is as wide as its
    // widest operand.
    if (result_size != 1 && (lhs_size == 2 || rhs_size == 2)) {
        result_size = 2;
    }
    return result_size;
}

/* ------------------------------------------------------------------ *
 * Entry point
 * ------------------------------------------------------------------ */
void m16_generate_top(Node *code, long count) {
    nxt_regstack_top = AST_MIN;

    // Global initialisers, collected into a routine crt0 calls before main.
    emit("__on_entry:\n");
    for (long j = 0; j < count; j++) {
        if (code[j].type == ND_ASSIGN) {
            generate(&code[j], NO_EXPECTED_SIZE);
        }
    }
    emit("ret\n");

    // Only function definitions produce code. A bare `char a;` at file scope is a
    // declaration; minc-8's codegen runs it through generate() anyway and emits a
    // stray (dead, but real) load between functions.
    for (long j = 0; j < count; j++) {
        if (code[j].type == ND_FUNC_DEF) {
            generate(&code[j], NO_EXPECTED_SIZE);
        }
    }
}
