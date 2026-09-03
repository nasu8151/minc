#include "codegen.h"

static int        nxt_regstack_top;
static int        cur_regstack_max;
static long       cur_arg_count;
static int        cur_return_size;
static bool       cur_is_isr;
static const int  ast_min = 2;
static const int  caller_max = 5;

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

// Lowest register index this function is responsible for preserving across a
// call. r0-r5 are caller-saved (see ND_FUNC_CALL), but an ISR has no software
// caller to have done that, so it owns everything the regstack machine touches.
// Both the prologue's pushes and the epilogue's pops read this, so they cannot
// disagree about how many registers are on the stack.
static int callee_save_lo(void) {
    return cur_is_isr ? ast_min : caller_max + 1;
}

void generate_top(Node *code, long i) {
    nxt_regstack_top = ast_min;
    emit("__on_entry:\n");
    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) { // Global variable assignment
            generate(&code[j], NO_EXPECTED_SIZE);
        }
    }
    emit("ret\n");

    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) {// Global variable assignment
            continue;
        }
        generate(&code[j], NO_EXPECTED_SIZE);
    }

}

unsigned long current_end = 0;

static unsigned long label_id = 0;

char *get_unique_label(bool isloopend) {
    char *label = calloc(20, sizeof(char));
    sprintf(label, "__L%ld", ++label_id);
    if (isloopend) {
        current_end = label_id;
    }
    return label;
}

// ND_FUNC_DEF generates each body twice (see there). Rewinding the label
// counter after the discarded pass keeps the labels that actually get emitted
// contiguous, so the output is unchanged from the single-pass days.
typedef struct { unsigned long id; unsigned long end; } LabelMark;

static LabelMark label_mark(void) {
    LabelMark m = { label_id, current_end };
    return m;
}

static void label_rewind(LabelMark m) {
    label_id = m.id;
    current_end = m.end;
}

char *get_break_label() {
    char *label = calloc(20, sizeof(char));
    sprintf(label, "__L%ld", current_end);
    return label;
}

// push value onto regstack
// returns current top
// nxt_regstack_top will be set to next top
//
// This only records how far up the register file the function reaches; it emits
// nothing. The matching push/pop pair is written once by the prologue/epilogue
// (see ND_FUNC_DEF). Emitting the save lazily at first use put it wherever that
// use happened to sit in the instruction stream, so a first use inside a loop
// pushed once per iteration while the single pop never ran -- one byte of stack
// leaked per iteration -- and a first use inside an untaken branch never pushed
// at all while the epilogue popped regardless.
int push_regstack(int size) {
    int top = nxt_regstack_top + size - 1;
    if (cur_regstack_max < top) {
        cur_regstack_max = top;
    }
    int cur = nxt_regstack_top;
    nxt_regstack_top = nxt_regstack_top + size;
    return cur;
}

// Bracket the two registers used as the X-pointer (r12:r13) around any code that
// clobbers them, but only inside an ISR -- an ISR has no software caller to have
// protected these (they're reloaded fresh via mvi/mov on every global/deref access
// and never expected to persist across other code otherwise).
static void isr_x_save(void) {
    if (cur_is_isr) {
        emit("push r13\n");
        emit("push r12\n");
    }
}

static void isr_x_restore(void) {
    if (cur_is_isr) {
        emit("pop r12\n");
        emit("pop r13\n");
    }
}

// Save/restore of the callee-owned registers, r<callee_save_lo()>..r<high>.
// Both sides derive their range from the same pair of values, so they cannot
// disagree about how many bytes are on the stack. The saves sit *above* the
// frame-pointer load in the prologue, which leaves Y pointing just below them --
// hence the epilogue restores SP from Y before popping, and the body is free to
// leave SP wherever it likes.
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

// pop value from regstack
// returns current top
int pop_regstack(int size) {
    nxt_regstack_top = nxt_regstack_top - size;
    return nxt_regstack_top;
}

// push and pop value on regstack
// returns current top
int chg_regstack(int size) {
    return nxt_regstack_top - size;
}

int set_regstack(int value) {
    int before = nxt_regstack_top;
    nxt_regstack_top = value;
    return before;
}

void generate_prologue(Node **args, long arg_reg_count, long local_var_count, int reg_high_water) {
    cur_arg_count = arg_reg_count;
    set_regstack(ast_min);
    emit("push r15\n");
    emit("push r14\n");
    emit_callee_saves(reg_high_water);
    emit("ldm r14,0\n"); // SP
    emit("ldm r15,1\n");

    long reg_index = ast_min;
    long mem_off = 0;

    if (args) {
        Node **arg = args;
        while (*arg) {
            Node *a = *arg;
            int arg_size = (a && a->valtype) ? a->valtype->size : 2;
            if (arg_size != 1 && arg_size != 2) {
                error_at(a ? a->loc : NULL, "Invalid argument size: %d", arg_size);
            }
            if (arg_size == 1) {
                emit("stm Y%ld,r%ld\n", -(mem_off + 1), reg_index);
                mem_off += 1;
                reg_index += 1;
            } else {
                // little-endian: low byte at lower address (more negative)
                emit("stm Y%ld,r%ld\n", -(mem_off + 2), reg_index);
                emit("stm Y%ld,r%ld\n", -(mem_off + 1), reg_index + 1);
                mem_off += 2;
                reg_index += 2;
            }
            arg++;
        }
    } else {
        // フォールバック（args が無い場合は従来通り）
        for (long i = 0; i < arg_reg_count; i++) {
            emit("stm %ld,r%ld\n", -i - 1, i + ast_min);
        }
    }

    emit("mvi r0,%ld\n", ((-local_var_count) & 0xFF));  // local_var_count includes arguments
    emit("mvi r1,%ld\n", ((-local_var_count) >> 8) & 0xFF);
    emit("add r0,r14\n");
    emit("adc r1,r15\n");
    emit("stm 0,r0\n");
    emit("stm 1,r1\n");
}

void generate_epilogue(int size, char *loc) {
    // 戻り値を先にr0:r1へ退避してから復元する。復元はcallee責任のレジスタを
    // 上書きするので、戻り値がそこに乗っている場合に壊れるため。
    int dst = pop_regstack(size);
    if (size == 1) {
        emit("mov r0,r%d\nmvi r1,0\n", dst);            // 戻り値をr0にセット(Little Endian)
    } else if (size == 2) {
        emit("mov r0,r%d\nmov r1,r%d\n", dst, dst + 1); // 戻り値をr0:r1にセット
    } else if (size == 0){
        // Nothing
    } else {
        error_at(loc, "Invalid size for return value: %d", size);
    }
    emit("stm 0,r14\n"); // SP = Y : 退避したレジスタの直上まで巻き戻す
    emit("stm 1,r15\n");
    emit_callee_restores(cur_regstack_max);
    emit("pop r14\n");
    emit("pop r15\n");
    emit("ret\n");
}

// ISR prologue/epilogue: no arguments, no return value (validated at parse time),
// r0/r1 saved unconditionally (the frame-size scratch arithmetic below clobbers
// them immediately, before any reactive push_regstack protection could apply),
// and reti instead of ret so PSR is restored from PSR_SHADOW (re-enabling IE).
void generate_isr_prologue(long local_var_count, int reg_high_water) {
    emit("push r1\n");
    emit("push r0\n");
    cur_arg_count = 0;
    set_regstack(ast_min);
    emit("push r15\n");
    emit("push r14\n");
    isr_x_save();
    emit_callee_saves(reg_high_water);
    emit("ldm r14,0\n"); // SP
    emit("ldm r15,1\n");

    emit("mvi r0,%ld\n", ((-local_var_count) & 0xFF));
    emit("mvi r1,%ld\n", ((-local_var_count) >> 8) & 0xFF);
    emit("add r0,r14\n");
    emit("adc r1,r15\n");
    emit("stm 0,r0\n");
    emit("stm 1,r1\n");
}

void generate_isr_epilogue(void) {
    emit("stm 0,r14\n"); // SP = Y : 退避したレジスタの直上まで巻き戻す
    emit("stm 1,r15\n");
    emit_callee_restores(cur_regstack_max);
    isr_x_restore();
    emit("pop r14\n");
    emit("pop r15\n");
    emit("pop r0\n");
    emit("pop r1\n");
    emit("reti\n");
}

int generate(Node *node, int size) {
    if (!node) return size;
    switch (node->type) {
    case ND_NUM: {
        int result_size = 0;
        if (size == NO_EXPECTED_SIZE) size = node->valtype ? node->valtype->size : 2; 
        int dst = push_regstack(size);
        if (size == 1) {
            if (node->val < -128 || node->val > 255) {
                error_at(node->loc, "Value out of range for char: %ld", node->val);
            }
            emit("mvi r%d,%ld\n", dst, node->val);
            result_size = 1;
        } else if (size == 2) {
            if (node->val < -32768 || node->val > 65535) {
                error_at(node->loc, "Value out of range for int: %ld", node->val);
            }
            emit("mvi r%d,%ld\n", dst, node->val & 0xFF);
            emit("mvi r%d,%ld\n", dst + 1, (node->val >> 8) & 0xFF);
            result_size = 2;
        } else {
            error_at(node->loc, "Invalid size for number literal: %ld", size);
        }
        return result_size;
    } case ND_LOCAL_VAR: {
        int actual = node->valtype->size;
        int expected = (size == NO_EXPECTED_SIZE) ? actual : size;

        if (actual == 1) {
            emit("ldm r%d,Y%ld\n", push_regstack(1), node->ofs_addr);
        } else if (actual == 2) {
            int dst = push_regstack(2);
            emit("ldm r%d,Y%ld\n", dst, node->ofs_addr);
            emit("ldm r%d,Y%ld\n", dst + 1, node->ofs_addr + 1);
        } else {
            error_at(node->loc, "Invalid size for local variable: %ld", node->valtype->size);
        }

        if (actual == 1 && expected == 2) {
            cast_i8_to_i16();
            return 2;
        }
        if (actual == 2 && expected == 1) {
            cast_i16_to_i8();
            return 1;
        }
        return actual;
    } case ND_GLOBAL_VAR: {
        int actual = node->valtype->size;
        int expected = (size == NO_EXPECTED_SIZE) ? actual : size;

        if (cur_is_isr) {
            // Reserve the destination register (and its protective push, if any)
            // BEFORE touching r12:r13, so the X-pointer save/restore below stays
            // strictly nested around the address load -- a push_regstack-triggered
            // push lives until the function epilogue, so it must never land
            // between our push r13/r12 and our matching pop.
            if (actual == 1) {
                int dst = push_regstack(1);
                emit("mvi r13,%ld\nmvi r12,%ld\n",
                    ((node->ofs_addr >> 8) & 0xFF), (node->ofs_addr & 0xFF));
                emit("ldm r%d,X+0\n", dst);
            } else if (actual == 2) {
                int dst = push_regstack(2);
                emit("mvi r13,%ld\nmvi r12,%ld\n",
                    ((node->ofs_addr >> 8) & 0xFF), (node->ofs_addr & 0xFF));
                emit("ldm r%d,X+0\nldm r%d,X+1\n", dst, dst + 1);
            } else {
                error_at(node->loc, "Invalid size for global variable: %ld", node->valtype->size);
            }
        } else {
            emit("mvi r13,%ld\nmvi r12,%ld\n",
                ((node->ofs_addr >> 8) & 0xFF), (node->ofs_addr & 0xFF));

            if (actual == 1) {
                int dst = push_regstack(1);
                emit("ldm r%d,X+0\n", dst);
            } else if (actual == 2) {
                int dst = push_regstack(2);
                emit("ldm r%d,X+0\nldm r%d,X+1\n", dst, dst + 1);
            } else {
                error_at(node->loc, "Invalid size for global variable: %ld", node->valtype->size);
            }
        }

        if (actual == 1 && expected == 2) {
            cast_i8_to_i16();
            return 2;
        }
        if (actual == 2 && expected == 1) {
            cast_i16_to_i8();
            return 1;
        }
        return actual;
    } case ND_ASSIGN: {
        int val_size = node->lhs->valtype->size;
        // `*p = e` needs the destination address pushed *below* the value, since
        // the ND_DEREF arm pops the value first. Emitting it here (rather than in
        // that arm) keeps the right-hand side evaluated exactly once for every
        // kind of destination -- generating it twice made `*p = f();` call f()
        // twice and leaked a regstack slot on each store.
        if (node->lhs->type == ND_DEREF) {
            generate(node->lhs->lhs, PTR_SIZE); // 左辺値（デリファレンスして得たアドレス）
        }
        // A void right-hand side (asm(...), sei()/cli(), a void function call)
        // pushes nothing onto the regstack, so the pops below would unbalance it.
        if (generate(node->rhs, val_size) == 0) {
            error_at(node->loc, "Cannot assign a value of size 0 (void)");
        }
        if (node->lhs->type == ND_LOCAL_VAR) {
            if (node->lhs->valtype->size == 1) {
                emit("stm Y%ld,r%d\n", node->lhs->ofs_addr, pop_regstack(1));
            } else if (node->lhs->valtype->size == 2) {
                int src = pop_regstack(2);
                emit("stm Y%ld,r%d\n", node->lhs->ofs_addr, src);
                emit("stm Y%ld,r%d\n", node->lhs->ofs_addr + 1, src + 1);
            } else {
                error_at(node->loc, "Invalid size for local variable: %ld", node->lhs->valtype->size);
            }
        } else if (node->lhs->type == ND_GLOBAL_VAR) {
            if (node->lhs->valtype->size == 1) {
                int src = pop_regstack(1);
                emit("mvi r13,%ld\nmvi r12,%ld\nstm X+0,r%d\n", ((node->lhs->ofs_addr >> 8) & 0xFF), (node->lhs->ofs_addr & 0xFF), src);
            } else if (node->lhs->valtype->size == 2) {
                int src = pop_regstack(2);
                emit("mvi r13,%ld\nmvi r12,%ld\n", ((node->lhs->ofs_addr >> 8) & 0xFF), (node->lhs->ofs_addr & 0xFF));
                emit("stm X+0,r%d\n", src);
                emit("stm X+1,r%d\n", src + 1);
            } else {
                error_at(node->loc, "Invalid size for global variable: %ld", node->lhs->valtype->size);
            }
        } else if (node->lhs->type == ND_DEREF) {
            // 右辺値（代入すべき値）と左辺値（アドレス）は既に上で積んである
            int val = pop_regstack(val_size);
            int addr = pop_regstack(PTR_SIZE);
            emit("mov r13,r%d\nmov r12,r%d\n", addr + 1, addr);

            if (val_size == 1) {
                emit("stm X+0,r%d\n", val);
            } else if (val_size == 2) {
                emit("stm X+0,r%d\n", val);
                emit("stm X+1,r%d\n", val + 1);
            } else {
                error_at(node->loc, "Invalid size for dereferenced assignment: %d", val_size);
            }
        } else {
            error_at(node->loc, "Left-hand side of assignment must be a variable");
        }
        return node->lhs->valtype->size;
    } case ND_RETURN: {
        if (cur_is_isr && node->lhs) {
            error_at(node->loc, "ISR functions cannot return a value");
        }
        if (node->lhs) {
            generate(node->lhs, cur_return_size);
            // generate(node->lhs);
        }
        if (cur_is_isr) {
            generate_isr_epilogue();
        } else {
            generate_epilogue(cur_return_size, node->loc);
        }
        return cur_return_size;
    } case ND_IF: {
        // generate(node->cond);
        generate(node->cond, 2); // 条件式
        int src = pop_regstack(2);
        int dst = push_regstack(1);
        emit("or r%d,r%d\n", dst, src);
        char *end_label = get_unique_label(false);
        emit("jz %s,r%d\n", end_label, pop_regstack(1));
        generate(node->lhs, NO_EXPECTED_SIZE); // then節
        // generate(node->lhs);
        emit("%s:\n", end_label);
        free(end_label);
        return 0;
    } case ND_IF_ELSE: {
        // generate(node->cond);
        generate(node->cond, 2); // 条件式
        int src = pop_regstack(2);
        int dst = push_regstack(1);
        emit("or r%d,r%d\n", dst, src);
        char *else_label = get_unique_label(false);
        char *end_label = get_unique_label(false);
        emit("jz %s,r%d\n", else_label, pop_regstack(1));
        // generate(node->lhs);
        generate(node->lhs, NO_EXPECTED_SIZE); // then節
        emit("jr %s\n", end_label);
        emit("%s:\n", else_label);
        generate(node->else_, NO_EXPECTED_SIZE); // else節
        // generate(node->else_);
        emit("%s:\n", end_label);
        free(else_label);
        free(end_label);
        return 0;
    } case ND_FOR: {
        if (node->init) {
            generate(node->init, NO_EXPECTED_SIZE);
            // generate(node->init);
        }
        char *begin_label = get_unique_label(false);
        char *end_label = get_unique_label(true);
        if (node->cond) {
            emit("%s:\n", begin_label);
            // generate(node->cond);
            generate(node->cond, 2); // 条件式
            int src = pop_regstack(2);
            int dst = push_regstack(1);
            emit("or r%d,r%d\n", dst, src);
            emit("jz %s,r%d\n", end_label, pop_regstack(1));
        } else {
            emit("%s:\n", begin_label);
        }
        generate(node->lhs, NO_EXPECTED_SIZE); // body
        // generate(node->lhs);
        if (node->inc) {
            generate(node->inc, NO_EXPECTED_SIZE);
            // generate(node->inc);
        }
        emit("jr %s\n", begin_label);
        emit("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return 0;
    } case ND_WHILE: {
        char *begin_label = get_unique_label(false);
        char *end_label = get_unique_label(true);
        emit("%s:\n", begin_label);
        // generate(node->cond);
        generate(node->cond, 2);
        int src = pop_regstack(2);
        int dst = push_regstack(1);
        emit("or r%d,r%d\n", dst, src);
        emit("jz %s,r%d\n", end_label, pop_regstack(1));
        // generate(node->lhs);
        generate(node->lhs, NO_EXPECTED_SIZE); // body
        emit("jr %s\n", begin_label);
        emit("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return 0;
    } case ND_BLOCK: {
        Node **member = node->body;
        while (*member) {
            generate(*member, NO_EXPECTED_SIZE);
            // generate(*member);
            member++;
        }
        return 0;
    } case ND_FUNC_DEF: {
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
        LabelMark mark = label_mark();
        g_mute++;
        cur_regstack_max = ast_min - 1; // "nothing touched yet"
        if (cur_is_isr) {
            generate_isr_prologue(node->lhs->arg_sf_size, ast_min - 1);
        } else {
            generate_prologue(node->body, node->arg_sf_size, node->lhs->arg_sf_size, ast_min - 1);
        }
        generate(node->lhs, NO_EXPECTED_SIZE);
        int reg_high_water = cur_regstack_max;
        g_mute--;
        label_rewind(mark); // keep the labels that actually get emitted contiguous

        emit("%s:\n", node->name);
        if (cur_is_isr) {
            generate_isr_prologue(node->lhs->arg_sf_size, reg_high_water);
        } else {
            generate_prologue(node->body, node->arg_sf_size, node->lhs->arg_sf_size, reg_high_water);
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
    } case ND_FUNC_CALL: {
        Node **arg = node->body;
        long arg_reg_count = 0;
        emit("push r1\n");
        emit("push r0\n");
        int before = set_regstack(ast_min);
        while (*arg) {
            Node *a = *arg;
            int arg_size = (a && a->valtype) ? a->valtype->size : 2;
            if (arg_size != 1 && arg_size != 2) {
                error_at(a ? a->loc : node->loc, "Invalid argument size: %d", arg_size);
            }
            if (arg_size == 1) {
                emit("push r%d\n", nxt_regstack_top);
            } else if (arg_size == 2) {
                emit("push r%ld\npush r%ld\n", nxt_regstack_top, nxt_regstack_top + 1);
            }
            generate(a, arg_size); // 引数を評価してregstackに積む
            arg++;
            arg_reg_count += arg_size;
        }
        for (int i = arg_reg_count + ast_min; i < caller_max + 1; i++) {
            emit("push r%d\n", i);
        }
        emit("calr %s\n", node->name);
        for (int i = (caller_max > arg_reg_count) ? caller_max : arg_reg_count; \
            ast_min <= i; i--) {
            emit("pop r%d\n", i);
        }
        set_regstack(before);

        int ret_size = (node->valtype) ? node->valtype->size : 2;
        if (ret_size == 1) {
            emit("mov r%d,r0\n", push_regstack(1));
        } else if (ret_size == 2) {
            int dst = push_regstack(2);
            emit("mov r%d,r0\nmov r%d,r1\n", dst, dst + 1);
        } else if (ret_size == 0) {
            // Nothing
        } else {
            error_at(node->loc, "Invalid return size: %d", ret_size);
        }
        emit("pop r0\n");
        emit("pop r1\n");
        return ret_size;    
    } case ND_BREAK: {
        emit("jr %s\n", get_break_label());
        return 0;
    } case ND_ADDR: {
        if (node->lhs->type == ND_LOCAL_VAR) {
            int dst = push_regstack(PTR_SIZE);
            emit("mov r%d,r15\nmov r%d,r14\n", dst + 1, dst); //ベースポインタをロード
            int ofs = push_regstack(PTR_SIZE);
            // オフセットを引く
            emit("mvi r%d,%ld\nmvi r%d,%ld\n", ofs + 1, (((node->lhs->ofs_addr >> 8) + 1) & 0xFF), ofs, (node->lhs->ofs_addr & 0xFF));
            int src = pop_regstack(PTR_SIZE);
            emit("add r%d,r%d\nadc r%d,r%d\n", dst + 1, ofs + 1, dst, ofs);
            return PTR_SIZE;
        }
        int dst = push_regstack(PTR_SIZE);
        emit("mvi r%d,%ld\nmvi r%d,%ld\n", dst + 1, ((node->lhs->ofs_addr >> 8) & 0xFF), dst, (node->lhs->ofs_addr & 0xFF));
        return PTR_SIZE;
    } case ND_DEREF: {
        // オペランドを評価してポインタを得る
        generate(node->lhs, PTR_SIZE);
        int ptr_reg = pop_regstack(PTR_SIZE);

        // ポインタが指す先の値をロード
        int dst = push_regstack(node->valtype->size);

        // r13:r12 にアドレスをセット
        emit("mov r13,r%d\nmov r12,r%d\n", ptr_reg + 1, ptr_reg);

        // デリファレンス結果の型サイズに応じてロード
        if (node->valtype->size == 1) {
            emit("ldm r%d,X+0\n", dst);
        } else if (node->valtype->size == 2) {
            emit("ldm r%d,X+0\nldm r%d,X+1\n", dst, dst + 1);
        } else {
            error_at(node->loc, "Invalid size for dereferenced value: %d", node->valtype->size);
        }
        return node->valtype->size;
    } case ND_ASM: {
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

    int lhs_size = generate(node->lhs, NO_EXPECTED_SIZE);
    int rhs_size = lhs_size;
    if (node->rhs) {
        rhs_size = generate(node->rhs, lhs_size);
    }

    if (lhs_size != rhs_size) {
        error_at(node->loc, "Type mismatch between left-hand side and right-hand side. Please cast explicitly.");
    }

    int result_size = 0;

    if (lhs_size == 1) {
        result_size = gen_i8(node);
    } else if (lhs_size == 2) {
        result_size = gen_i16(node);
    } else {
        error_at(node->loc, "Invalid size for binary operation: %d", lhs_size);
    }
    if (result_size == 1 && size == 2) {
        cast_i8_to_i16();
        return 2;
    } else if (result_size == 2 && size == 1) {
        cast_i16_to_i8();
        return 1;
    }

    return result_size;
}

int gen_i8(Node *node) {

    int src;
    if (!(node->type == ND_NOT))
        src = pop_regstack(1);
    int dst = chg_regstack(1);

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
    case ND_EQ:
        emit("sub r%d,r%d\nchz r%d,r%d\n", dst, src, dst, dst);
        break;
    case ND_NEQ:
        emit("sub r%d,r%d\nchz r%d,r%d\nchz r%d,r%d\n", dst, src, dst, dst, dst, dst);
        break;
    case ND_LT:
        emit("lt r%d,r%d\n", dst, src);
        break;
    case ND_GE:
        emit("lt r%d,r%d\nchz r%d,r%d\n", dst, src, dst, dst);
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
    case ND_AND:
        emit("chz r%d,r%d\nchz r%d,r%d\nor r%d,r%d\nchz r%d,r%d\n", dst, dst, src, src, dst, src, dst, dst);
        break;
    case ND_OR:
        emit("chz r%d,r%d\nchz r%d,r%d\nand r%d,r%d\nchz r%d,r%d\n", dst, dst, src, src, dst, src, dst, dst);
        break;
    case ND_NOT:
        emit("chz r%d,r%d\n", dst, dst);
        break;
    default:
        error_at(node->loc, "Unknown node type");
        break;
    }
    return 1;
}

int gen_i16(Node *node) {
    int src = pop_regstack(2);
    int dst = chg_regstack(2);

    switch (node->type) {
    case ND_ADD:
        emit("add r%d,r%d\nadc r%d,r%d\n", dst, src, dst + 1, src + 1);
        break;
    case ND_SUB:
        emit("sub r%d,r%d\nsbc r%d,r%d\n", dst, src, dst + 1, src + 1);
        break;
    case ND_EQ:
        emit("sub r%d,r%d\nsbc r%d,r%d\nor r%d,r%d\nchz r%d,r%d\nmvi r%d,0\n", dst, src, dst + 1, src + 1, dst, dst + 1, dst, dst, dst + 1);
        break;
    case ND_NEQ:
        emit("sub r%d,r%d\nsbc r%d,r%d\nor r%d,r%d\nmvi r%d,0\nlt r%d,r%d\n", dst, src, dst + 1, src + 1, dst + 1, dst, dst, dst, dst + 1);
        break;
    case ND_LT:
        emit("sub r%d,r%d\nltc r%d,r%d\nmov r%d,r%d\nmvi r%d,0\n", dst, src, dst + 1, src + 1, dst, dst + 1, dst + 1);
        break;
    case ND_GE:
        emit("sub r%d,r%d\nltc r%d,r%d\nchz r%d,r%d\nmvi r%d,0\n", dst, src, dst + 1, src + 1, dst, dst + 1, dst + 1);
        break;
    case ND_BITWISE_AND:
        emit("and r%d,r%d\nand r%d,r%d\n", dst, src, dst + 1, src + 1);
        break;
    case ND_BITWISE_OR:
        emit("and r%d,r%d\nand r%d,r%d\n", dst, src, dst + 1, src + 1);
        break;
    case ND_BITWISE_XOR:
        emit("and r%d,r%d\nand r%d,r%d\n", dst, src, dst + 1, src + 1);
        break;
    case ND_AND:
        emit("or r%d,r%d\nor r%d,r%d\nchz r%d,r%d\nchz r%d,r%d\nor r%d,r%d\nchz r%d,r%d\nmvi r%d,0\n", dst, dst + 1, src, src + 1, dst, dst, src, src, dst, src, dst, dst, dst + 1);
        break;
    case ND_OR:
        emit("or r%d,r%d\nor r%d,r%d\nchz r%d,r%d\nchz r%d,r%d\nand r%d,r%d\nchz r%d,r%d\nmvi r%d,0\n", dst, dst + 1, src, src + 1, dst, dst, src, src, dst, src, dst, dst, dst + 1);
        break;
    case ND_NOT:
        emit("or r%d,r%d\nchz r%d,r%d\nmvi r%d,0\n", dst, dst + 1, dst, dst, dst + 1);
        break;
    default:
        error_at(node->loc, "Unknown node type");
        break;
    }
    return 2;
}

int cast_i8_to_i16() {
    int dst = push_regstack(1);
    emit("mvi r%d,0\n", dst);
    return 2;
}

int cast_i16_to_i8() {
    pop_regstack(1); // 上位バイトを捨てる
    return 1;
}
