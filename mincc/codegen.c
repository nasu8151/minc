#include "codegen.h"

static int        nxt_regstack_top;
static int        cur_regstack_max;
static long       cur_arg_count;
static int        cur_return_size;
static const int  ast_min = 2;
static const int  caller_max = 5;

void generate_top(Node *code, long i) {
    nxt_regstack_top = ast_min;
    printf("__on_entry:\n");
    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) { // Global variable assignment
            generate(&code[j], NO_EXPECTED_SIZE);
        }
    }
    printf("ret\n");

    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) {// Global variable assignment
            continue;
        }
        generate(&code[j], NO_EXPECTED_SIZE);
    }

}

unsigned long current_end = 0;

char *get_unique_label(bool isloopend) {
    static unsigned long label_id = 0;
    char *label = calloc(20, sizeof(char));
    sprintf(label, "__L%ld", ++label_id);
    if (isloopend) {
        current_end = label_id;
    }
    return label;
}

char *get_break_label() {
    char *label = calloc(20, sizeof(char));
    sprintf(label, "__L%ld", current_end);
    return label;
}

// push value onto regstack
// returns current top
// nxt_regstack_top will be set to next top
int push_regstack(int size) {
    if (cur_regstack_max < nxt_regstack_top) {
        cur_regstack_max = nxt_regstack_top;
        if (caller_max < nxt_regstack_top) { // callee責任のレジスタは自分で退避
            if ((nxt_regstack_top % 2) == 0) {
                printf("push r%d\n", nxt_regstack_top);
            }
        }
    }
    int cur = nxt_regstack_top;
    nxt_regstack_top = nxt_regstack_top + size;
    return cur;
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

void generate_prologue(long arg_count, long local_var_count) {
    cur_arg_count = arg_count;
    cur_regstack_max = ast_min;
    nxt_regstack_top = ast_min;
    printf("push r14\n");
    printf("lds r14\n");
    for (long i = 0; i < arg_count; i++) {
        printf("stm %ld,r%ld\n", -i - 1, i + ast_min); // 引数をメモリに展開
    }
    printf("mvi r0,%ld\n", ((-local_var_count) & 0xFF));  // local_var_count includes arguments
    printf("mvi r1,%ld\n", ((-local_var_count) >> 8) & 0xFF);
    printf("add r0,r14\n");
    printf("adc r1,r15\n");
    printf("sts r0\n");
}

void generate_epilogue(long arg_count, int size, char *loc) {
    for (long i = cur_regstack_max; i >= ((caller_max > arg_count) ? caller_max : arg_count); i--) {
        if ((i % 2) == 0) {
            printf("pop r%ld\n", i);
        }
    } // callee責任分（argの分は含まず）を回収する
    int dst = pop_regstack(size);
    if (size == 1) {
        printf("mov r0,r%d\nmvi r1,0\n", dst);            // 戻り値をr0にセット(Little Endian)
    } else if (size == 2) {
        printf("mov r0,r%d\nmov r1,r%d\n", dst, dst + 1); // 戻り値をr0:r1にセット
    } else {
        error_at(loc, "Invalid size for return value: %d", size);
    }
    printf("sts r14\n");
    printf("pop r14\n");
    printf("ret\n");
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
            printf("mvi r%d,%ld\n", dst, node->val);
            result_size = 1;
        } else if (size == 2) {
            if (node->val < -32768 || node->val > 65535) {
                error_at(node->loc, "Value out of range for int: %ld", node->val);
            }
            printf("mvi r%d,%ld\n", dst, node->val & 0xFF);
            printf("mvi r%d,%ld\n", dst + 1, (node->val >> 8) & 0xFF);
            result_size = 2;
        } else {
            error_at(node->loc, "Invalid size for number literal: %ld", size);
        }
        return result_size;
    } case ND_LOCAL_VAR: {
        if (node->valtype->size == 1) {
            printf("ldm r%d,%ld\n", push_regstack(1), node->ofs_addr);
        } else if (node->valtype->size == 2) {
            int dst = push_regstack(2);
            printf("ldm r%d,%ld\n", dst, node->ofs_addr);
            printf("ldm r%d,%ld\n", dst + 1, node->ofs_addr + 1);
        } else {
            error_at(node->loc, "Invalid size for local variable: %ld", node->valtype->size);
        }
        return node->valtype->size;
    } case ND_GLOBAL_VAR: {
        printf("mvi r13,%ld\nmvi r12,%ld\nldm r%d,X+0\n", ((node->ofs_addr >> 8) & 0xFF), (node->ofs_addr & 0xFF), push_regstack(1));
        return node->valtype->size;
    } case ND_ASSIGN: {
        // generate(node->rhs);
        generate(node->rhs, node->lhs->valtype->size);
        if (node->lhs->type == ND_LOCAL_VAR) {
            if (node->lhs->valtype->size == 1) {
                printf("stm %ld,r%d\n", node->lhs->ofs_addr, pop_regstack(1));
            } else if (node->lhs->valtype->size == 2) {
                int src = pop_regstack(2);
                printf("stm %ld,r%d\n", node->lhs->ofs_addr, src);
                printf("stm %ld,r%d\n", node->lhs->ofs_addr + 1, src + 1);
            } else {
                error_at(node->loc, "Invalid size for local variable: %ld", node->lhs->valtype->size);
            }
        } else if (node->lhs->type == ND_GLOBAL_VAR) {
            if (node->lhs->valtype->size == 1) {
                printf("mvi r13,%ld\nmvi r12,%ld\nstm X+0,r%d\n", ((node->lhs->ofs_addr >> 8) & 0xFF), (node->lhs->ofs_addr & 0xFF), pop_regstack(1));
            } else if (node->lhs->valtype->size == 2) {
                int src = pop_regstack(2);
                printf("mvi r13,%ld\nmvi r12,%ld\n", ((node->lhs->ofs_addr >> 8) & 0xFF), (node->lhs->ofs_addr & 0xFF));
                printf("stm X+0,r%d\n", src);
                printf("stm X+1,r%d\n", src + 1);
            } else {
                error_at(node->loc, "Invalid size for global variable: %ld", node->lhs->valtype->size);
            }
        } else if (node->lhs->type == ND_DEREF) {
            error_at(node->loc, "Under construction: assignment to dereferenced pointer");
            // generate(node->lhs->lhs, PTR_SIZE);
            // long addr = pop_regstack(PTR_SIZE);
            // long value = pop_regstack(node->lhs->valtype->size);
            // printf("mvi r12,%ld\nmvi r13,%ld\nstm X+0,r%d\n", ((addr >> 8) & 0xFF), (addr & 0xFF), value);
        } else {
            error_at(node->loc, "Left-hand side of assignment must be a variable");
        }
        return node->lhs->valtype->size;
    } case ND_RETURN: {
        if (node->lhs) {
            generate(node->lhs, cur_return_size);
            // generate(node->lhs);
        }
        generate_epilogue(cur_arg_count, cur_return_size, node->loc);
        return cur_return_size;
    } case ND_IF: {
        // generate(node->cond);
        generate(node->cond, 2); // 条件式
        int src = pop_regstack(2);
        int dst = push_regstack(1);
        printf("or r%d,r%d\n", dst, src);
        char *end_label = get_unique_label(false);
        printf("jz %s,r%d\n", end_label, pop_regstack(1));
        generate(node->lhs, NO_EXPECTED_SIZE); // then節
        // generate(node->lhs);
        printf("%s:\n", end_label);
        free(end_label);
        return 0;
    } case ND_IF_ELSE: {
        // generate(node->cond);
        generate(node->cond, 2); // 条件式
        int src = pop_regstack(2);
        int dst = push_regstack(1);
        printf("or r%d,r%d\n", dst, src);
        char *else_label = get_unique_label(false);
        char *end_label = get_unique_label(false);
        printf("jz %s,r%d\n", else_label, pop_regstack(1));
        // generate(node->lhs);
        generate(node->lhs, NO_EXPECTED_SIZE); // then節
        printf("jr %s\n", end_label);
        printf("%s:\n", else_label);
        generate(node->else_, NO_EXPECTED_SIZE); // else節
        // generate(node->else_);
        printf("%s:\n", end_label);
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
            printf("%s:\n", begin_label);
            // generate(node->cond);
            generate(node->cond, 2); // 条件式
            int src = pop_regstack(2);
            int dst = push_regstack(1);
            printf("or r%d,r%d\n", dst, src);
            printf("jz %s,r%d\n", end_label, pop_regstack(1));
        } else {
            printf("%s:\n", begin_label);
        }
        generate(node->lhs, NO_EXPECTED_SIZE); // body
        // generate(node->lhs);
        if (node->inc) {
            generate(node->inc, NO_EXPECTED_SIZE);
            // generate(node->inc);
        }
        printf("jr %s\n", begin_label);
        printf("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return 0;
    } case ND_WHILE: {
        char *begin_label = get_unique_label(false);
        char *end_label = get_unique_label(true);
        printf("%s:\n", begin_label);
        // generate(node->cond);
        generate(node->cond, 2);
        int src = pop_regstack(2);
        int dst = push_regstack(1);
        printf("or r%d,r%d\n", dst, src);
        printf("jz %s,r%d\n", end_label, pop_regstack(1));
        // generate(node->lhs);
        generate(node->lhs, NO_EXPECTED_SIZE); // body
        printf("jr %s\n", begin_label);
        printf("%s:\n", end_label);
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
        printf("%s:\n", node->name);
        if (node->lhs->type != ND_BLOCK) {
            error_at(node->loc, "Function body must be a block");
        }
        generate_prologue(node->arg_sf_size, node->lhs->arg_sf_size);
        // generate(node->lhs); // function body
        generate(node->lhs, NO_EXPECTED_SIZE); // function body
        generate_epilogue(cur_arg_count, cur_return_size, node->loc);
        return cur_return_size;
    } case ND_FUNC_CALL: {
        Node **arg = node->body;
        long arg_count = 0;
        printf("push r0\n");
        int before = set_regstack(ast_min);
        while (*arg) {
            Node *a = *arg;
            if (nxt_regstack_top % 2 == 0) {
                printf("push r%d\n", nxt_regstack_top);
            }
            // generate(a);
            generate(a, 1/*絶対に直せ！！！！！！！！*/); // 引数を評価してregstackに積む
            arg++;
            arg_count++;
        }
        for (int i = arg_count + ast_min; i < caller_max + 1; i++) {
            if ((i % 2) == 0) {
                printf("push r%d\n", i);
            }
        }
        printf("calr %s\n", node->name);
        for (int i = (caller_max > arg_count) ? caller_max : arg_count; \
            ast_min <= i; i--) {
            if ((i % 2) == 0) {
                printf("pop r%d\n", i);
            }
        }
        set_regstack(before);
        printf("mov r%d,r0\npop r0\n", push_regstack(1));
        return node->valtype->size;
    } case ND_BREAK: {
        printf("jr %s\n", get_break_label());
        return 0;
    } case ND_ADDR: {
        if (node->lhs->type == ND_LOCAL_VAR) {
            int dst = push_regstack(PTR_SIZE);
            printf("mov r%d,r13\nmov r%d,r12\n", dst + 1, dst);
            int ofs = push_regstack(PTR_SIZE);
            printf("mvi r%d,%ld\nmvi r%d,%ld\n", ofs + 1, ofs, ((node->lhs->ofs_addr >> 8) & 0xFF), (node->lhs->ofs_addr & 0xFF));
            int src = pop_regstack(PTR_SIZE);
            printf("add r%d,%d\nadc r%d,%d", dst + 1, ofs + 1, dst, ofs);
            return PTR_SIZE;
        }
        int dst = push_regstack(PTR_SIZE);
        printf("mvi r%d,%ld\nmvi r%d, %ld", dst + 1, dst, ((node->lhs->ofs_addr >> 8) & 0xFF), (node->lhs->ofs_addr & 0xFF));
        return PTR_SIZE;
    } case ND_DEREF: {
        gen_deref(node, node->valtype, 0);
    }
    default:
        break;
    }

    int lhs_size = generate(node->lhs, NO_EXPECTED_SIZE);
    int rhs_size = 0;
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

    int src = pop_regstack(1);
    int dst = chg_regstack(1);

    switch (node->type) {
    case ND_ADD:
        printf("add r%d,r%d\n", dst, src);
        break;
    case ND_SUB:
        printf("sub r%d,r%d\n", dst, src);
        break;
    case ND_MUL:
        printf("mul r%d,r%d\n", dst, src);
        break;
    case ND_EQ:
        printf("sub r%d,r%d\nmvi r0,1\nlt r%d,r0\n", dst, src, dst);
        break;
    case ND_NEQ:
        printf("sub r%d,r%d\nmvi r0,0\nlt r0,r%d\nmov r%d,r0\n", dst, src, dst, dst);
        break;
    case ND_LT:
        printf("lt r%d,r%d\n", dst, src);
        break;
    case ND_GE:
        printf("lt r%d,r%d\nmvi r0,1\nlt r%d,r0\npush r0\n", dst, src, dst);
        break;
    case ND_BITWISE_AND:
        printf("and r%d,r%d\n", dst, src);
        break;
    case ND_BITWISE_OR:
        printf("or r%d,r%d\n", dst, src);
        break;
    case ND_BITWISE_XOR:
        printf("xor r%d,r%d\n", dst, src);
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
        printf("add r%d,r%d\nadc r%d,r%d\n", dst, src, dst + 1, src + 1);
        break;
    case ND_SUB:
        printf("sub r%d,r%d\nsbc r%d,r%d\n", dst, src, dst + 1, src + 1);
        break;
    default:
        error_at(node->loc, "Unknown node type");
        break;
    }
    return 2;
}

Type_t *gen_deref(Node *node, Type_t *valtype, int recurce) {
    if (!valtype) {
        error_at(node->loc, "Dereferencing the value which is not a pointer nor an array.");
    }
    int src = chg_regstack(PTR_SIZE);
    if (node->type != ND_DEREF) {
        int dst = chg_regstack(valtype->size);
        printf("mov r13,%d\nmov r12,r%d", src, src + 1);
        if (valtype->size == 1) {

        }
        return node->valtype;
    }
    Type_t *valtype = gen_deref(node->lhs, node->valtype->ptr_to, recurce + 1);
}

int cast_i8_to_i16() {
    int dst = push_regstack(1);
    printf("mvi r%d,0\n", dst);
    return 2;
}

int cast_i16_to_i8() {
    pop_regstack(1); // 上位バイトを捨てる
    return 1;
}
