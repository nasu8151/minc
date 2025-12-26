#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

Node code[256];

// Create new node (type != ND_NUM)
Node *new_node(NodeType type, Node *lhs, Node *rhs){
    Node *node = calloc(1, sizeof(Node));
    node->type = type;
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

// Create new node (type == ND_NUM)
Node *new_num_node(long val) {
    Node *node = calloc(1, sizeof(Node));
    node->type = ND_NUM;
    node->val = val;
    return node;
}

Node *new_ident_node(char *name) {
    Node *node = calloc(1, sizeof(Node));
    node->type = ND_LOC_VAR;
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
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
stmt       = expr ";"
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
        code[i++] = *stmt();
    }
    code[i] = *new_node(ND_EOF, NULL, NULL);

    for (long j = 0; j < i; j++) {
        generate(&code[j]);
    }
}

Node *stmt() {
    Node *node = expr();
    expect(";");
    return node;
}

Node *expr() {
    Node *node = assign();
    return node;
}

Node *assign() {
    Node *node = equality();

    if (consume("=")) {
        node = new_node(ND_ASSIGN, node, assign());
    }
    return node;
}

Node *equality() {
    Node *node = relational();

    while (true) {
        if (consume("==")) {
            node = new_node(ND_EQ, node, relational());
        } else if (consume("!=")) {
            node = new_node(ND_NEQ, node, relational());
        } else {
            return node;
        }
    }
}

Node *relational() {
    Node *node = add();

    while (true) {
        if (consume("<=")) {
            node = new_node(ND_GE, node, add());
        } else if(consume(">=")) {
            node = new_node(ND_LE, node, add());
        } else if (consume(">")) {
            node = new_node(ND_GT, node, add());
        } else if (consume("<")) {
            node = new_node(ND_LT, node, add());
        } else {
            return node;
        }
    }
}

Node *add() {
    Node *node = mul();
    
    while (true) {
        if (consume("+")) {
            node = new_node(ND_ADD, node, mul());
        } else if (consume("-")) {
            node = new_node(ND_SUB, node, mul());
        } else {
            return node;
        }
    }
}

Node *mul() {
    Node *node = unary();

    while (true) {
        if (consume("*")) {
            node = new_node(ND_MUL, node, unary());
        } else {
            return node;
        }
    }
}

Node *primary() {       // primary = num | ident | "(" expr ")"
    if (consume("(")) { // かっこがあるなら、"(" expr ")"のはず
        Node *node = expr();

        expect(")"); // かっこは閉じられるはず...
        return node;
    } else if (is_number_node()) {         // numの部分
        return new_num_node(expect_number());
    } else {                               // identの部分
        return new_ident_node(expect_ident());
    }
}

Node *unary() {
    if (consume("+")) {
        return new_node(ND_ADD, new_num_node(0), unary());
    } else if (consume("-")) {
        return new_node(ND_SUB, new_num_node(0), unary());
    } else {
        return primary();
    }
}

void generate(Node *node) {
    if(node->type == ND_NUM) {
        printf("mvi r0,%ld\npush r0\n", node->val);
        return;
    } else if (node->type == ND_LOC_VAR) {
        printf("ld r0,%s\npush r0\n", node->name);
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
        error("Unknown node type");
        break;
    }
}
