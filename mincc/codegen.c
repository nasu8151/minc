#include "codegen.h"

static long       nxt_regstack_top;
static long       cur_regstack_max;
static long       cur_arg_count;
static const long ast_min = 2;
static const long caller_max = 5;

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
long push_regstack() {
    if (cur_regstack_max < nxt_regstack_top) {
        cur_regstack_max = nxt_regstack_top;
        if (caller_max < nxt_regstack_top) { // callee責任のレジスタは自分で退避
            if ((nxt_regstack_top & 1) == 0) {
                printf("push r%ld\n", nxt_regstack_top);
            }
        }
    }
    return nxt_regstack_top++;
}

// pop value from regstack
// returns current top
long pop_regstack() {
    nxt_regstack_top = nxt_regstack_top - 1;
    return nxt_regstack_top;
}

// push and pop value on regstack
// returns current top
long chg_regstack() {
    return nxt_regstack_top - 1;
}

void generate_prologue(long arg_count, long local_var_count) {
    cur_arg_count = arg_count;
    cur_regstack_max = ast_min;
    nxt_regstack_top = ast_min;
    printf("push r14\n");
    printf("lds r15\n");
    for (long i = 0; i < arg_count; i++) {
        printf("stm %ld,r%ld\n", -i - 1, i + ast_min); // 引数をメモリに展開
    }
    printf("mvi r0,%ld\n", -local_var_count);  // local_var_count includes arguments
    printf("add r0,r15\n");
    printf("sts r0\n");
}

void generate_epilogue(long arg_count) {
    for (long i = cur_regstack_max; i >= ((caller_max > arg_count) ? caller_max : arg_count); i--) {
        if ((i & 1) == 0) {
            printf("pop r%ld\n", i);
        }
    } // callee責任分（argの分は含まず）を回収する
    printf("mov r0,r%ld\n", pop_regstack());
    printf("sts r15\n");
    printf("pop r14\n");
    printf("ret\n");
}

void generate(Node *node) {
    if (!node) return;
    switch (node->type) {
    case ND_NUM: {
        printf("mvi r%ld,%ld\n", push_regstack(), node->val);
        return;
    } case ND_LOCAL_VAR: {
        printf("ldm r%ld,%ld\n", push_regstack(), node->ofs_addr);
        return;
    } case ND_GLOBAL_VAR: {
        printf("mov r14,r15\nmvi r15,%ld\nldm r%ld,0\nmov r15,r14\n", node->ofs_addr, push_regstack());
        return;
    } case ND_ASSIGN: {
        generate(node->rhs);
        if (node->lhs->type == ND_LOCAL_VAR) {
            printf("stm %ld,r%ld\n", node->lhs->ofs_addr, pop_regstack());
        } else if (node->lhs->type == ND_GLOBAL_VAR) {
            printf("mov r14,r15\nmvi r15,%ld\nstm 0,r%ld\nmov r15,r14\n", node->lhs->ofs_addr, pop_regstack());
        } else if (node->lhs->type == ND_DEREF) {
            generate(node->lhs->lhs);
            long addr = pop_regstack();
            long value = pop_regstack();
            printf("mov r14,r15\nmov r15,r%ld\nstm 0,r%ld\nmov r15,r14\n", addr, value);
        } else {
            error_at(node->loc, "Left-hand side of assignment must be a variable");
        }
        return;
    } case ND_RETURN: {
        if (node->lhs) {
            generate(node->lhs);
        }
        generate_epilogue(cur_arg_count);
        return;
    } case ND_IF: {
        generate(node->cond); // 条件式
        char *end_label = get_unique_label(false);
        printf("jz %s,r%ld\n", end_label, pop_regstack());
        generate(node->lhs); // then節
        printf("%s:\n", end_label);
        free(end_label);
        return;
    } case ND_IF_ELSE: {
        generate(node->cond); // 条件式
        char *else_label = get_unique_label(false);
        char *end_label = get_unique_label(false);
        printf("jz %s,r%ld\n", else_label, pop_regstack());
        generate(node->lhs); // then節
        printf("jr %s\n", end_label);
        printf("%s:\n", else_label);
        generate(node->else_); // else節
        printf("%s:\n", end_label);
        free(else_label);
        free(end_label);
        return;
    } case ND_FOR: {
        if (node->init) {
            generate(node->init);
        }
        char *begin_label = get_unique_label(false);
        char *end_label = get_unique_label(true);
        if (node->cond) {
            printf("%s:\n", begin_label);
            generate(node->cond);
            printf("jz %s,r%ld\n", end_label, pop_regstack());
        } else {
            printf("%s:\n", begin_label);
        }
        generate(node->lhs); // body
        if (node->inc) {
            generate(node->inc);
        }
        printf("jr %s\n", begin_label);
        printf("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return;
    } case ND_WHILE: {
        char *begin_label = get_unique_label(false);
        char *end_label = get_unique_label(true);
        printf("%s:\n", begin_label);
        generate(node->cond);
        printf("jz %s,r%ld\n", end_label, pop_regstack());
        generate(node->lhs); // body
        printf("jr %s\n", begin_label);
        printf("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return;
    } case ND_BLOCK: {
        Node **member = node->body;
        while (*member) {
            generate(*member);
            member++;
        }
        return;
    } case ND_FUNC_DEF: {
        printf("%s:\n", node->name);
        if (node->lhs->type != ND_BLOCK) {
            error_at(node->loc, "Function body must be a block");
        }
        generate_prologue(node->arg_sf_size, node->lhs->arg_sf_size);
        generate(node->lhs); // function body
        generate_epilogue(cur_arg_count);
        return;
    } case ND_FUNC_CALL: {
        Node **arg = node->body;
        long arg_count = 0;
        while (*arg) {
            generate(*arg);
            arg++;
            arg_count++;
        }
        printf("push r0\n");
        for (long i = ast_min; i < caller_max + 1; i++) {
            if ((i & 1) == 0) {
                printf("push r%ld\n", i);
            }
            if (i < ast_min + arg_count) {
                printf("mov r%ld,r%ld\n", ast_min + arg_count, pop_regstack());
            } // r2, r3, ... に引数をセット
        }
        printf("calr %s\n", node->name);
        for (long i = (caller_max > arg_count) ? caller_max : arg_count; \
            ast_min <= i; i--) {
            if ((i & 1) == 0) {
                printf("pop r%ld\n", i);
            }
        }
        printf("mov r%ld,r0\npop r0\n", push_regstack());
        return;
    } case ND_BREAK: {
        printf("jr %s\n", get_break_label());
        return;
    } case ND_ADDR: {
        if (node->lhs->type == ND_LOCAL_VAR) {
            printf("mov r%ld,r15\n", push_regstack());
            printf("mvi r%ld,%ld\n", push_regstack(), node->lhs->ofs_addr);
            long src = pop_regstack();
            long dst = chg_regstack();
            printf("add r%ld,r%ld\n", dst, src);
            return;
        }
        printf("mvi r%ld,%ld\n", push_regstack(), node->lhs->ofs_addr);
        return;
    } case ND_DEREF: {
        generate(node->lhs);
        printf("mov r14,r15\nmov r15,r%ld\nldm r%ld,0\nmov r15,r14\n",chg_regstack(), chg_regstack());
        return;
    }
    default:
        break;
    }

    generate(node->lhs);
    if (node->rhs) generate(node->rhs);

    long src = pop_regstack();
    long dst = chg_regstack();

    switch (node->type) {
    case ND_ADD:
        printf("add r%ld,r%ld\n", dst, src);
        break;
    case ND_SUB:
        printf("sub r%ld,r%ld\n", dst, src);
        break;
    case ND_MUL:
        printf("mul r%ld,r%ld\n", dst, src);
        break;
    case ND_EQ:
        printf("sub r%ld,r%ld\nmvi r0,1\nlt r%ld,r0\n", dst, src, dst);
        break;
    case ND_NEQ:
        printf("sub r%ld,r%ld\nmvi r0,0\nlt r0,r%ld\nmov r%ld,r0\n", dst, src, dst, dst);
        break;
    case ND_LT:
        printf("lt r%ld,r%ld\n", dst, src);
        break;
    case ND_GE:
        printf("lt r%ld,r%ld\nmvi r0,1\nlt r%ld,r0\npush r0\n", dst, src, dst);
        break;
    case ND_BITWISE_AND:
        printf("and r%ld,r%ld\n", dst, src);
        break;
    case ND_BITWISE_OR:
        printf("or r%ld,r%ld\n", dst, src);
        break;
    case ND_BITWISE_XOR:
        printf("xor r%ld,r%ld\n", dst, src);
        break;
    default:
        error_at(node->loc, "Unknown node type");
        break;
    }
}
