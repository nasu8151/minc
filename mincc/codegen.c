#include "codegen.h"

void generate_top(Node *code, long i) {
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

char *get_unique_label() {
    static long label_id = 0;
    char *label = calloc(20, sizeof(char));
    sprintf(label, "__L%ld", label_id++);
    return label;
}

void generate_prologue(long arg_count, long local_var_count) {
    printf("push r15\n");
    printf("lds r15\n");
    for (long i = 0; i < arg_count; i++) {
        printf("push r%ld\n", i + 2);
    }
    printf("mvi r0,%ld\n", -local_var_count);  // local_var_count includes arguments
    printf("add r0,r15\n");
    printf("sts r0\n");
}

void generate_epilogue() {
    printf("pop r0\n");
    printf("sts r15\n");
    printf("pop r15\n");
    printf("ret\n");
}

void generate(Node *node) {
    switch (node->type) {
    case ND_NUM: {
        printf("mvi r0,%ld\npush r0\n", node->val);
        return;
    } case ND_LOCAL_VAR: {
        printf("ldm r0,%ld\npush r0\n", node->ofs_addr);
        return;
    } case ND_GLOBAL_VAR: {
        printf("push r15\nmvi r15,%ld\nldm r0,0\npop r15\npush r0\n", node->ofs_addr);
        return;
    } case ND_ASSIGN: {
        generate(node->rhs);
        if (node->lhs->type == ND_LOCAL_VAR) {
            printf("pop r0\nstm %ld,r0\n", node->lhs->ofs_addr);
        } else if (node->lhs->type == ND_GLOBAL_VAR) {
            printf("pop r0\npush r15\nmvi r15,%ld\nstm 0,r0\npop r15\n", node->lhs->ofs_addr);
        } else {
            error_at(node->loc, "Left-hand side of assignment must be a variable");
        }
        return;
    } case ND_RETURN: {
        generate(node->lhs);
        generate_epilogue();
        return;
    } case ND_IF: {
        generate(node->cond); // 条件式
        char *end_label = get_unique_label();
        printf("pop r0\njz %s,r0\n", end_label);
        generate(node->lhs); // then節
        printf("%s:\n", end_label);
        free(end_label);
        return;
    } case ND_IF_ELSE: {
        generate(node->cond); // 条件式
        char *else_label = get_unique_label();
        char *end_label = get_unique_label();
        printf("pop r0\njz %s,r0\n", else_label);
        generate(node->lhs); // then節
        printf("mvi r0,0\njz %s,r0\n", end_label);
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
        char *begin_label = get_unique_label();
        char *end_label = get_unique_label();
        if (node->cond) {
            printf("%s:\n", begin_label);
            generate(node->cond);
            printf("pop r0\njz %s,r0\n", end_label);
        } else {
            printf("%s:\n", begin_label);
        }
        generate(node->lhs); // body
        if (node->inc) {
            generate(node->inc);
        }
        printf("mvi r0,0\njz %s,r0\n", begin_label);
        printf("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return;
    } case ND_WHILE: {
        char *begin_label = get_unique_label();
        char *end_label = get_unique_label();
        printf("%s:\n", begin_label);
        generate(node->cond);
        printf("pop r0\njz %s,r0\n", end_label);
        generate(node->lhs); // body
        printf("mvi r0,0\njz %s,r0\n", begin_label);
        printf("%s:\n", end_label);
        free(begin_label);
        free(end_label);
        return;
    } case ND_BLOCK: {
        NodeList_Member *member = node->body;
        while (member) {
            generate(member->node);
            member = member->next;
        }
        return;
    } case ND_FUNC_DEF: {
        printf("%s:\n", node->name);
        if (node->lhs->type != ND_BLOCK) {
            error_at(node->loc, "Function body must be a block");
        }
        generate_prologue(node->arg_sf_size, node->lhs->arg_sf_size);
        generate(node->lhs); // function body
        generate_epilogue();
        return;
    } case ND_FUNC_CALL: {
        NodeList_Member *arg = node->body;
        long arg_count = 0;
        while (arg) {
            generate(arg->node);
            printf("pop r%ld\n", 2 + arg_count); // r2, r3, ... に引数をセット
            arg = arg->next;
            arg_count++;
        }
        printf("call %s\npush r0\n", node->name);
        return;
    }
    default:
        break;
    }

    generate(node->lhs);
    generate(node->rhs);

    switch (node->type) {
    case ND_ADD:
        printf("pop r1\npop r0\nadd r0,r1\npush r0\n");
        break;
    case ND_SUB:
        printf("pop r1\npop r0\nsub r0,r1\npush r0\n");
        break;
    case ND_MUL:
        printf("pop r1\npop r0\nmul r0,r1\npush r0\n");
        break;
    case ND_EQ:
        printf("pop r1\npop r0\nsub r0,r1\nmvi r2,1\nlt r0,r2\npush r0\n");
        break;
    case ND_NEQ:
        printf("pop r1\npop r0\nsub r0,r1\nmvi r2,0\nlt r2,r0\npush r2\n");
        break;
    case ND_LT:
        printf("pop r1\npop r0\nlt r0,r1\npush r0\n");
        break;
    case ND_GE:
        printf("pop r1\npop r0\nlt r0,r1\nmvi r2,1\nlt r0,r2\npush r0\n");
        break;
    default:
        error_at(node->loc, "Unknown node type");
        break;
    }
}
