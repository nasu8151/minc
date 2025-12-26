#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

Node code[256];

LocalVar *local_vars = NULL;

// Create new node (type != ND_NUM)
Node *new_node(NodeType type, Node *lhs, Node *rhs, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    node->type = type;
    node->lhs = lhs;
    node->rhs = rhs;
    node->loc = loc;
    return node;
}

// Create new node (type == ND_NUM)
Node *new_num_node(long val, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    node->type = ND_NUM;
    node->val = val;
    node->loc = loc;
    return node;
}

Node *new_ident_node(char *name, long offset, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    node->type = ND_LOC_VAR;
    node->offset = offset;
    node->name = mystrndup(name, strlen(name));
    node->name_len = strlen(name);
    node->loc = loc;
    return node;
}

void print_node(Node *node) {
    if (node->type == ND_NUM) {
        printf("ND_NUM: %ld\n", node->val);
    } else if (node->type == ND_LOC_VAR) {
        printf("ND_LOC_VAR: %s\n", node->name);
    } else {
        printf("Node type: %d\n", node->type);
        if (node->lhs) {
            printf("LHS:\n");
            print_node(node->lhs);
        }
        if (node->rhs) {
            printf("RHS:\n");
            print_node(node->rhs);
        }
    }
}

/***************************************************************
program    = stmt*
stmt       = expr ";" | "return" expr ";"
expr       = assign
assign     = equality ("=" assign)?
equality   = relational ("==" relational | "!=" relational)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
add        = mul ("+" mul | "-" mul)*
mul        = unary ("*" unary | "/" unary)*
unary      = ("+" | "-")? primary
primary    = num | ident | "(" expr ")"
****************************************************************/

void program() {
    long i = 0;
    while (!at_eof()) {
        code[i++] = *stmt(token->loc);
    }
    code[i] = *new_node(ND_EOF, NULL, NULL, token->loc);

    generate_prologue(count_local_vars());
    for (long j = 0; j < i; j++) {
        generate(&code[j]);
    }
}

Node *stmt(char *l) {
    Node *node;
    char *loc = l;
    if (consume("return", loc)) {
        node = new_node(ND_RETURN, expr(loc), NULL, loc);
        expect(";", loc);
    } else {
        node = expr(loc);
        expect(";", loc);
    }
    return node;
}

Node *expr(char *l) {
    char *loc = l;
    Node *node = assign(loc);
    return node;
}

Node *assign(char *l) {
    char *loc = l;
    Node *node = equality(loc);

    if (consume("=", loc)) {
        node = new_node(ND_ASSIGN, node, assign(loc), loc);
    }
    return node;
}

Node *equality(char *l) {
    char *loc = l;
    Node *node = relational(loc);

    while (true) {
        if (consume("==", loc)) {
            node = new_node(ND_EQ, node, relational(loc), loc);
        } else if (consume("!=", loc)) {
            node = new_node(ND_NEQ, node, relational(loc), loc);
        } else {
            return node;
        }
    }
}

Node *relational(char *l) {
    char *loc = l;
    Node *node = add(loc);

    while (true) {
        if (consume("<=", loc)) {
            node = new_node(ND_GE, node, add(loc), loc);
        } else if(consume(">=", loc)) {
            node = new_node(ND_LE, node, add(loc), loc);
        } else if (consume(">", loc)) {
            node = new_node(ND_GT, node, add(loc), loc);
        } else if (consume("<", loc)) {
            node = new_node(ND_LT, node, add(loc), loc);
        } else {
            return node;
        }
    }
}

Node *add(char *l) {
    char *loc = l;
    Node *node = mul(loc);
    
    while (true) {
        if (consume("+", loc)) {
            node = new_node(ND_ADD, node, mul(loc), loc);
        } else if (consume("-", loc)) {
            node = new_node(ND_SUB, node, mul(loc), loc);
        } else {
            return node;
        }
    }
}

Node *mul(char *l) {
    char *loc = l;
    Node *node = unary(loc);

    while (true) {
        if (consume("*", loc)) {
            node = new_node(ND_MUL, node, unary(loc), loc);
        } else {
            return node;
        }
    }
}

Node *primary(char *l) {       // primary = num | ident | "(" expr ")"
    char *loc = l;
    if (consume("(", loc)) { // かっこがあるなら、"(" expr ")"のはず
        Node *node = expr(loc);

        expect(")", loc); // かっこは閉じられるはず...
        return node;
    } else if (is_number_node()) {         // numの部分
        return new_num_node(expect_number(loc), loc);
    } else {                               // identの部分
        LocalVar *var = find_local_var(token);
        Token *tok = token;
        char *name = expect_ident(loc);
        long offset;
        if (var) {
            offset = var->offset;
            // fprintf(stderr, "Found local variable: %s at offset %ld\n", name, offset);
        } else {
            add_local_var(tok);
            // fprintf(stderr, "Added local variable: %s at offset %ld\n", name, local_vars->offset);
            offset = local_vars->offset;
        }
        return new_ident_node(name, offset, loc);
    }
}

Node *unary(char *l) {
    char *loc = l;
    if (consume("+", loc)) {
        return new_node(ND_ADD, new_num_node(0, loc), unary(loc), loc);
    } else if (consume("-", loc)) {
        return new_node(ND_SUB, new_num_node(0, loc), unary(loc), loc);
    } else {
        return primary(loc);
    }
}

LocalVar *find_local_var(Token *tok) {
    LocalVar *var = local_vars;
    while (var) {
        // fprintf(stderr, "Comparing %s with %s\n", var->name, tok->str);
        if (strcmp(var->name, tok->str) == 0) {
            return var;
        }
        var = var->next;
    }
    return NULL;
}

void add_local_var(Token *tok) {
    LocalVar *var = calloc(1, sizeof(LocalVar));
    var->name_len = tok->size;
    var->name = mystrndup(tok->str, tok->size);
    var->offset = (local_vars ? local_vars->offset - 1 : -1);
    var->next = local_vars;
    local_vars = var;
}

long count_local_vars() {
    long count = 0;
    LocalVar *var = local_vars;
    while (var) {
        count++;
        var = var->next;
    }
    return count;
}

void generate_prologue(long local_var_count) {
    printf("push r15\n");
    printf("lds r15\n");
    printf("mvi r0,%ld\n", -local_var_count);  // ローカル変数の分の領域を確保
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
    if(node->type == ND_NUM) {
        printf("mvi r0,%ld\npush r0\n", node->val);
        return;
    } else if (node->type == ND_LOC_VAR) {
        printf("ldm r0,%ld\npush r0\n", node->offset);
        return;
    } else if (node->type == ND_ASSIGN) {
        generate(node->rhs);
        if (node->lhs->type != ND_LOC_VAR) {
            error_at(node->lhs->loc, "Left-hand side of assignment must be a variable");
        }
        printf("pop r0\nstm %ld,r0\n", node->lhs->offset);
        return;
    } else if (node->type == ND_RETURN) {
        generate(node->lhs);
        generate_epilogue();
        return;
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
    case ND_LE:
        printf("pop r1\npop r0\nlt r1,r0\nmvi r2,1\nlt r1,r2\npush r1\n");
        break;
    case ND_GT:
        printf("pop r1\npop r0\nlt r1,r0\npush r1\n");
        break;
    case ND_GE:
        printf("pop r1\npop r0\nlt r0,r1\nmvi r2,1\nlt r0,r2\npush r0\n");
        break;
    default:
        error_at(node->loc, "Unknown node type");
        break;
    }
}
