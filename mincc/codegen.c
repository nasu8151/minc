#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

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

/***************************************************************
expr       = equality
equality   = relational ("==" relational | "!=" relational)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
add        = mul ("+" mul | "-" mul)*
mul        = unary ("*" unary | "/" unary)*
unary      = ("+" | "-")? primary
primary    = num | "(" expr ")"
****************************************************************/

Node *expr() {
    Node *node = equality();
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

Node *primary() {       // primary = num | "(" expr ")"
    if (consume("(")) { // かっこがあるなら、"(" expr ")"のはず
        Node *node = expr();

        expect(")"); // かっこは閉じられるはず...
        return node;
    } else {         // numの部分
        return new_num_node(expect_number());
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
