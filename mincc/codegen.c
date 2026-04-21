#include "codegen.h"

static int        nxt_regstack_top;
static int        cur_regstack_max;
static long       cur_arg_count;
static int        current_return_size;
static const int  ast_min = 2;
static const int  caller_max = 5;

void generate_top(Node *code, long i) {
    nxt_regstack_top = ast_min;
    printf("__on_entry:\n");
    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) { // Global variable assignment
            generate(&code[j]);
        }
    }
    printf("ret\n");

    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) {// Global variable assignment
            continue;
        }
        generate(&code[j]);
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
            if ((nxt_regstack_top & 1) == 0) {
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

void generate_prologue(long arg_count, long local_var_count) {
    cur_arg_count = arg_count;
    cur_regstack_max = ast_min;
    nxt_regstack_top = ast_min;
    printf("push r14\n");
    printf("lds r14\n");
    for (long i = 0; i < arg_count; i++) {
        printf("stm %ld,r%d\n", -i - 1, i + ast_min); // 引数をメモリに展開
    }
    printf("mvi r1,%ld\n", ((-local_var_count) & 0xFF));  // local_var_count includes arguments
    printf("mvi r0,%ld\n", ((-local_var_count) >> 8) & 0xFF);
    printf("add r1,r15\n");
    printf("adc r0,r14\n");
    printf("sts r0\n");
}

void generate_epilogue(long arg_count) {
    for (long i = cur_regstack_max; i >= ((caller_max > arg_count) ? caller_max : arg_count); i--) {
        if ((i & 1) == 0) {
            printf("pop r%d\n", i);
        }
    } // callee責任分（argの分は含まず）を回収する
    printf("mov r0,r%d\n", pop_regstack(current_return_size));
    printf("sts r14\n");
    printf("pop r14\n");
    printf("ret\n");
}

int generate(Node *node) {
    int size = 1;
    if (!node) return size;
    switch (node->type) {
    case ND_NUM: {
        int dst = push_regstack(size);
        if (size == 1) {
            if (node->val < -128 || node->val > 255) {
                error_at(node->loc, "Value out of range for char: %ld", node->val);
            }
            printf("mvi r%d,%ld\n", dst, node->val);
        } else if (size == 2) {
            if (node->val < -32768 || node->val > 65535) {
                error_at(node->loc, "Value out of range for int: %ld", node->val);
            }
            printf("mvi r%d,%ld\n", dst, node->val & 0xFF);
            printf("mvi r%d,%ld\n", dst + 1, (node->val >> 8) & 0xFF);
        } else {
            error_at(node->loc, "Invalid size for number literal: %ld", size);
        }
        return size;
    } case ND_LOCAL_VAR: {
        printf("ldm r%d,%ld\n", push_regstack(size), node->ofs_addr);
        return size;
    } case ND_GLOBAL_VAR: {
        printf("mvi r12,%ld\nmvi r13,%ld\nldm r%d,X+0\n", ((node->ofs_addr >> 8) & 0xFF), (node->ofs_addr & 0xFF), push_regstack(1));
        return size;
    } case ND_ASSIGN: {
        generate(node->rhs);
        // generate(node->rhs, node->lhs->valtype->size);
        if (node->lhs->type == ND_LOCAL_VAR) {
            printf("stm %ld,r%d\n", node->lhs->ofs_addr, pop_regstack(size));
        } else if (node->lhs->type == ND_GLOBAL_VAR) {
            printf("mvi r12,%ld\nmvi r13,%ld\nstm X+0,r%d\n", ((node->lhs->ofs_addr >> 8) & 0xFF), (node->lhs->ofs_addr & 0xFF), pop_regstack(size));
        } else if (node->lhs->type == ND_DEREF) {
            error_at(node->loc, "Under construction: assignment to dereferenced pointer");
            // generate(node->lhs->lhs, PTR_SIZE);
            // long addr = pop_regstack(PTR_SIZE);
            // long value = pop_regstack(node->lhs->valtype->size);
            // printf("mvi r12,%ld\nmvi r13,%ld\nstm X+0,r%d\n", ((addr >> 8) & 0xFF), (addr & 0xFF), value);
        } else {
            error_at(node->loc, "Left-hand side of assignment must be a variable");
        }
        return size;
    } case ND_RETURN: {
        if (node->lhs) {
            // generate(node->lhs, current_return_size);
            generate(node->lhs);
        }
        generate_epilogue(cur_arg_count);
        return size;
    } case ND_IF: {
        generate(node->cond);
        // generate(node->cond, 2); // 条件式
        // int src = pop_regstack(2);
        // int dst = push_regstack(1);
        // printf("or r%d,r%d\n", dst, src);
        char *end_label = get_unique_label(false);
        printf("jz %s,r%d\n", end_label, pop_regstack(1));
        // generate(node->lhs, -1); // then節
        generate(node->lhs);
        printf("%s:\n", end_label);
        free(end_label);
        return size;
    } case ND_IF_ELSE: {
        generate(node->cond);
        // generate(node->cond, 2); // 条件式
        // int src = pop_regstack(2);
        // int dst = push_regstack(1);
        // printf("or r%d,r%d\n", dst, src);
        char *else_label = get_unique_label(false);
        char *end_label = get_unique_label(false);
        printf("jz %s,r%d\n", else_label, pop_regstack(1));
        generate(node->lhs);
        // generate(node->lhs, -1); // then節
        printf("jr %s\n", end_label);
        printf("%s:\n", else_label);
        // generate(node->else_, -1); // else節
        generate(node->else_);
        printf("%s:\n", end_label);
        free(else_label);
        free(end_label);
        return size;
    } case ND_FOR: {
        if (node->init) {
            // generate(node->init, -1);
            generate(node->init);
        }
        char *begin_label = get_unique_label(false);
        char *end_label = get_unique_label(true);
        if (node->cond) {
            printf("%s:\n", begin_label);
            generate(node->cond);
            // generate(node->cond, 2); // 条件式
            // int src = pop_regstack(2);
            // int dst = push_regstack(1);
            // printf("or r%d,r%d\n", dst, src);
            printf("jz %s,r%d\n", end_label, pop_regstack(1));
        } else {
            printf("%s:\n", begin_label);
        }
        // generate(node->lhs, 2); // body
        generate(node->lhs);
        if (node->inc) {
            // generate(node->inc, 2);
            generate(node->inc);
        }
        printf("jr %s\n", begin_label);
        printf("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return size;
    } case ND_WHILE: {
        char *begin_label = get_unique_label(false);
        char *end_label = get_unique_label(true);
        printf("%s:\n", begin_label);
        generate(node->cond);
        // generate(node->cond, 2);
        // int src = pop_regstack(2);
        // int dst = push_regstack(1);
        // printf("or r%d,r%d\n", dst, src);
        printf("jz %s,r%d\n", end_label, pop_regstack(1));
        generate(node->lhs);
        // generate(node->lhs, -1); // body
        printf("jr %s\n", begin_label);
        printf("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return size;
    } case ND_BLOCK: {
        Node **member = node->body;
        while (*member) {
            // generate(*member, -1);
            generate(*member);
            member++;
        }
        return size;
    } case ND_FUNC_DEF: {
        current_return_size = 1;
        printf("%s:\n", node->name);
        if (node->lhs->type != ND_BLOCK) {
            error_at(node->loc, "Function body must be a block");
        }
        generate_prologue(node->arg_sf_size, node->lhs->arg_sf_size);
        generate(node->lhs); // function body
        // generate(node->lhs, -1); // function body
        generate_epilogue(cur_arg_count);
        return size;
    } case ND_FUNC_CALL: {
        Node **arg = node->body;
        long arg_count = 0;
        printf("push r0\n");
        while (*arg) {
            Node *a = *arg;
            generate(a);
            // generate(a, a->valtype->size); // 引数を評価してregstackに積む
            arg++;
            arg_count++;
        }
        for (long i = ast_min; i < caller_max + 1; i++) {
            if ((i & 1) == 0) {
                printf("push r%d\n", i);
            }
            if (i < ast_min + arg_count) {
                printf("mov r%d,r%d\n", ast_min + arg_count, pop_regstack(1));
            } // r2, r3, ... に引数をセット
        }
        printf("calr %s\n", node->name);
        for (long i = (caller_max > arg_count) ? caller_max : arg_count; \
            ast_min <= i; i--) {
            if ((i & 1) == 0) {
                printf("pop r%d\n", i);
            }
        }
        printf("mov r%d,r0\npop r0\n", push_regstack(1));
        return size;
    } case ND_BREAK: {
        printf("jr %s\n", get_break_label());
        return size;
    } case ND_ADDR: {
        if (node->lhs->type == ND_LOCAL_VAR) {
            printf("mov r%d,r15\n", push_regstack(1));
            printf("mvi r%d,%ld\n", push_regstack(1), node->lhs->ofs_addr);
            long src = pop_regstack(1);
            long dst = chg_regstack(1);
            printf("add r%d,r%d\n", dst, src);
            return size;
        }
        printf("mvi r%d,%ld\n", push_regstack(1), node->lhs->ofs_addr);
        return size;
    } case ND_DEREF: {
        generate(node->lhs);
        printf("push r14\nmov r15,r%d\nldm r%d,0\npop r14\n",chg_regstack(1), chg_regstack(1));
        return size;
    }
    default:
        break;
    }

    generate(node->lhs);
    if (node->rhs) generate(node->rhs);

    gen_i8(node);

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
}

int gen_i16(Node *node) {

}
