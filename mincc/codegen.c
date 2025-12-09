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

Node *expr() {
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

Node *primary() {       // primary = num | ("(" expr ")")
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
        printf("push %ld\n", node->val);
        return;
    }

    generate(node->lhs);
    generate(node->rhs);

    switch (node->type) {
    case ND_ADD:
        printf("add\n");
        break;
    case ND_SUB:
        printf("sub\n");
        break;
    case ND_MUL:
        printf("mul\n");
        break;
    default:
        error("Unknown node type");
        break;
    }
}
