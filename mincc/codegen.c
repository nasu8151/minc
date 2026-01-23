#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

Node code[256];

Vars_List *head = NULL;
Vars_List *current = NULL;
Vars_List *tail = NULL;

// Create new node (type != ND_NUM)
Node *new_node(NodeType type, Node *lhs, Node *rhs, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = type;
    node->lhs = lhs;
    node->rhs = rhs;
    node->loc = loc;
    return node;
}

Node *new_if_else_node(NodeType type, Node *cond, Node *then, Node *else_, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = type;
    node->cond = cond;
    node->lhs = then;
    node->else_ = else_;
    node->loc = loc;
    return node;
}

Node *new_for_node(Node *cond, Node *inc, Node *init, Node *body, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = ND_FOR;
    node->cond = cond;
    node->inc = inc;
    node->init = init;
    node->lhs = body;
    node->loc = loc;
    return node;
}

Node *new_while_node(Node *cond, Node *body, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = ND_WHILE;
    node->cond = cond;
    node->lhs = body;
    node->loc = loc;
    return node;
}

// Create new node (type == ND_NUM)
Node *new_num_node(long val, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = ND_NUM;
    node->val = val;
    node->loc = loc;
    return node;
}

Node *new_ident_node(NodeType type, char *name, long ofs_addr, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = type;
    node->ofs_addr = ofs_addr;
    node->name = mystrndup(name, strlen(name));
    node->name_len = strlen(name);
    node->loc = loc;
    return node;
}

Node *new_func_node(char *name, Node *body, long stack_frame_size, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = ND_FUNC_DEF;
    node->name = mystrndup(name, strlen(name));
    node->name_len = strlen(name);
    node->lhs = body;
    node->stack_frame_size = stack_frame_size;
    node->loc = loc;
    return node;
}

Node *new_node_vec() {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = ND_BLOCK;
    node->body = NULL;
    return node;
}

NodeVec_Member *add_node_vec_member(Node *node) {
    NodeVec_Member *member = calloc(1, sizeof(NodeVec_Member));
    if (!member) {
        error("Memory allocation failed");
    }
    member->node = node;
    member->next = NULL;
    return member;
}

void print_node(Node *node) {
    if (node->type == ND_NUM) {
        fprintf(stderr, "ND_NUM: %ld\n", node->val);
    } else if (node->type == ND_LOCAL_VAR) {
        fprintf(stderr, "ND_LOCAL_VAR: %s at offset %ld\n", node->name, node->ofs_addr);
    } else if (node->type == ND_GLOBAL_VAR) {
        fprintf(stderr, "ND_GLOBAL_VAR: %s at address %ld\n", node->name, node->ofs_addr);
    } else {
        fprintf(stderr, "Node type: %d\n", node->type);
        if (node->lhs) {
            fprintf(stderr, "(LHS:\n");
            print_node(node->lhs);
            fprintf(stderr, ")\n");
        }
        if (node->rhs) {
            fprintf(stderr, "(RHS:\n");
            print_node(node->rhs);
            fprintf(stderr, ")\n");
        }
        if (node->cond) {
            fprintf(stderr, "(COND:\n");
            print_node(node->cond);
            fprintf(stderr, ")\n");
        }
        if (node->else_) {
            fprintf(stderr, "(ELSE:\n");
            print_node(node->else_);
            fprintf(stderr, ")\n");
        }
        if (node->init) {
            fprintf(stderr, "(INIT:\n");
            print_node(node->init);
            fprintf(stderr, ")\n");
        }
        if (node->inc) {
            fprintf(stderr, "(INC:\n");
            print_node(node->inc);
            fprintf(stderr, ")\n");
        }
        if (node->body) {
            fprintf(stderr, "(BODY:\n");
            NodeVec_Member *cur = node->body;
            while (cur) {
                print_node(cur->node);
                cur = cur->next;
            }
            fprintf(stderr, ")\n");
        }
    }
}

/***************************************************************
program     = toplevel*
toplevel    = ident "=" assign ";" | ident "(" ")" stmt  <-- must be a block
stmt        = expr ";"
            | "{" stmt* "}"
            | "return" expr ";"
            | "if" "(" expr ")" stmt ("else" stmt)?
            | "for" "(" expr? ";" expr? ";" expr? ")" stmt
            | "while" "(" expr ")" stmt
expr       = assign
assign     = equality ("=" assign)?
equality   = relational ("==" relational | "!=" relational)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
add        = mul ("+" mul | "-" mul)*
mul        = unary ("*" unary)*
unary      = ("+" | "-")? primary
primary    = num | ident | ident "(" ")" | "(" expr ")"
****************************************************************/

void program() {
    long i = 0;
    head = calloc(1, sizeof(Vars_List));
    head->parent = NULL;
    tail = head;
    while (!at_eof()) {
        code[i++] = *toplevel(token->loc);
    }
    code[i] = *new_node(ND_EOF, NULL, NULL, token->loc);

    for (long j = 0; j < i; j++) {
        print_node(&code[j]);
    }

    printf("__on_entry:\n");
    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) // Global variable assignment
            generate(&code[j]);
    }
    printf("ret\n");

    for (long j = 0; j < i; j++) {
        if (code[j].type == ND_ASSIGN) // Global variable assignment
            continue;
        generate(&code[j]);
    }
}

Node *toplevel(char *loc) {
    char *l = loc;
    Token *tok = token;
    char *name = expect_ident(l);
    if (consume_la("(", l)) {
        expect(")", l);
        Node *node = new_func_node(name, stmt(l), 0, l);
    } else {
        expect("=", l);
        Node *rhs = assign(l);
        expect(";", l);
        add_global_var(tok);
        Node *node = new_node(ND_ASSIGN, new_ident_node(ND_GLOBAL_VAR, name, head->var_tail->address, l), rhs, l);
        return node;
    }
}

Node *stmt(char *l) {
    Node *node;
    char *loc = l;
    if (consume_la("return", loc)) {
        node = new_node(ND_RETURN, expr(loc), NULL, loc);
        expect(";", loc);
    } else if (consume_la("if", loc)) {
        expect("(", loc);
        Node *cond = expr(loc);
        expect(")", loc);
        Node *then = stmt(loc);
        Node *els = NULL;
        if (consume_la("else", loc)) {
            els = stmt(loc);
        }
        if (els) {
            node = new_if_else_node(ND_IF_ELSE, cond, then, els, loc);
        } else {
            node = new_if_else_node(ND_IF, cond, then, NULL, loc);
        }
    } else if (consume_la("for", loc)) {
        expect("(", loc);
        Node *init = NULL;
        if (!consume_la(";", loc)) {
            init = expr(loc);
            expect(";", loc);
        }
        Node *cond = NULL;
        if (!consume_la(";", loc)) {
            cond = expr(loc);
            expect(";", loc);
        }
        Node *inc = NULL;
        if (!consume_la(")", loc)) {
            inc = expr(loc);
            expect(")", loc);
        }
        Node *body = stmt(loc);
        node = new_for_node(cond, inc, init, body, loc);
    } else if (consume_la("while", loc)) {
        expect("(", loc);
        Node *cond = expr(loc);
        expect(")", loc);
        Node *body = stmt(loc);
        node = new_while_node(cond, body, loc);
    } else if (consume_la("{", loc)) {
        new_scope();
        node = new_node_vec();
        NodeVec_Member *head = calloc(1, sizeof(NodeVec_Member));
        if (!head) {
            error("Memory allocation failed");
        }
        NodeVec_Member *cur = head;
        while (!consume("}", loc)) {
            cur->next = add_node_vec_member(stmt(loc));
            cur = cur->next;
        }
        node->body = head->next;
        node->stack_frame_size = end_scope();
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

    if (consume_la("=", loc)) {
        node = new_node(ND_ASSIGN, node, assign(loc), loc);
    }
    return node;
}

Node *equality(char *l) {
    char *loc = l;
    Node *node = relational(loc);

    while (true) {
        if (consume_la("==", loc)) {
            node = new_node(ND_EQ, node, relational(loc), loc);
        } else if (consume_la("!=", loc)) {
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
        if       (consume_la("<=", loc)) {
            node = new_node(ND_GE, add(loc), node, loc);
        } else if(consume_la(">=", loc)) {
            node = new_node(ND_GE, node, add(loc), loc);
        } else if (consume_la(">", loc)) {
            node = new_node(ND_LT, add(loc), node, loc);
        } else if (consume_la("<", loc)) {
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
        if (consume_la("+", loc)) {
            node = new_node(ND_ADD, node, mul(loc), loc);
        } else if (consume_la("-", loc)) {
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
        if (consume_la("*", loc)) {
            node = new_node(ND_MUL, node, unary(loc), loc);
        } else {
            return node;
        }
    }
}

Node *primary(char *l) {       // primary = num | ident | "(" expr ")"
    char *loc = l;
    if (consume_la("(", loc)) { // かっこがあるなら、"(" expr ")"のはず
        Node *node = expr(loc);

        expect(")", loc); // かっこは閉じられるはず...
        return node;
    } else if (is_number_node()) {         // numの部分
        return new_num_node(expect_number(loc), loc);
    } else {                               // identの部分
        Ident_Name *var = find_var(token);
        Token *tok = token;
        char *name = expect_ident(loc);
        long offset;
        if (var) {
            if (var->type == VAR_GLOBAL_STATIC) {
                fprintf(stderr, "Found global variable: %s at address %ld\n", name, var->address);
                return new_ident_node(ND_GLOBAL_VAR, name, var->address, loc);
            } else {
                fprintf(stderr, "Found variable: %s at offset %ld\n", name, var->offset);
                return new_ident_node(ND_LOCAL_VAR, name, var->offset, loc);
            }
        } else {
            if (tail->parent == NULL) { // グローバルスコープならグローバル変数として追加
                add_global_var(tok);
                fprintf(stderr, "Added global variable: %s at address %ld\n", name, head->var_tail->address);
                return new_ident_node(ND_GLOBAL_VAR, name, head->var_tail->address, loc);
            } else {
                add_local_var(tok);
                fprintf(stderr, "Added local variable: %s at offset %ld\n", name, current->var_tail->offset);
                return new_ident_node(ND_LOCAL_VAR, name, current->var_tail->offset, loc);
            }
        }
        error_at(loc, "Undefined variable: %s", name);
    }
}

Node *unary(char *l) {
    char *loc = l;
    if (consume_la("+", loc)) {
        return new_node(ND_ADD, new_num_node(0, loc), unary(loc), loc);
    } else if (consume_la("-", loc)) {
        return new_node(ND_SUB, new_num_node(0, loc), unary(loc), loc);
    } else {
        return primary(loc);
    }
}

Ident_Name *find_var(Token *tok) {
    Vars_List *cur = tail;
    while (cur) {
        Ident_Name *var = cur->var_head;
        while (var) {
            fprintf(stderr, "Comparing %s with %s\n", var->name, tok->str);
            if (strcmp(var->name, tok->str) == 0) {
                return var;
            }
            var = var->next;
        }
        cur = cur->parent;
    }
    return NULL;
}

void add_local_var(Token *tok) {
    Ident_Name *var = calloc(1, sizeof(Ident_Name));
    if (!var) {
        error("Memory allocation failed");
    }
    var->name_len = tok->size;
    var->name = mystrndup(tok->str, tok->size);
    var->type = VAR_LOCAL;
    var->address = 0; // ローカル変数のアドレスは0に設定
    var->next = NULL;
    Vars_List *cur = tail;
    if (!cur) {
        error_at(tok->loc, "No variable scope available");
    }
    Ident_Name *lvar = cur->var_tail;
    if (!lvar) {
        cur->var_head = var;
        cur->var_tail = var;
    } else {
        cur->var_tail->next = var;
        cur->var_tail = var;
    }
    cur->max_vars_count++;
    var->offset = 0 - count_local_vars(); // スタック上のオフセットを設定
}

void add_global_var(Token *tok) {
    Ident_Name *var = calloc(1, sizeof(Ident_Name));
    if (!var) {
        error("Memory allocation failed");
    }
    var->name_len = tok->size;
    var->name = mystrndup(tok->str, tok->size);
    var->type = VAR_GLOBAL_STATIC;
    var->next = NULL;
    Ident_Name *gvar = head->var_head;
    if (!gvar) {
        head->var_head = var;
        head->var_tail = var;
    } else {
        head->var_tail->next = var;
        head->var_tail = var;
    }
    var->offset = 0;  // グローバル変数のオフセットは0に設定
    head->max_vars_count++;
    var->address = count_global_vars(); // グローバル変数のアドレスを設定
    fprintf(stderr, "Adding global variable: %.*s at address %ld\n", (int)tok->size, tok->str, var->address);
}

long count_local_vars() {
    long count = 0;
    Vars_List *cur = head->child;
    while (cur) {
        Ident_Name *var = cur->var_head;
        while (var) {
            count++;
            var = var->next;
        }
        cur = cur->child;
    }
    return count;
}

long count_global_vars() {
    long count = 0;
    Ident_Name *var = head->var_head;
    while (var) {
        count++;
        var = var->next;
    }
    return count;
}

void new_scope() {
    Vars_List *new_list = calloc(1, sizeof(Vars_List));
    if (!new_list) {
        error("Memory allocation failed");
    }
    new_list->parent = tail;
    if (tail) {
        tail->child = new_list;
    }
    tail = new_list;
    current = tail;
}

long end_scope() {
    if (tail) {
        if (tail->child) {
            error("Cannot end scope with active child scope");
        }
        if (!tail->parent) {
            error("Cannot end global scope");
        }
        long current_max_vars_count = count_local_vars();
        if (tail->parent->max_vars_count < current_max_vars_count) {
            tail->parent->max_vars_count = current_max_vars_count;
        }
        Vars_List *old_tail = tail;
        tail = tail->parent;
        free(old_tail);
        tail->child = NULL;
        current = tail;
    } else {
        error("No scope to end");
    }
    return tail->max_vars_count;
}

char *get_unique_label() {
    static long label_id = 0;
    char *label = calloc(20, sizeof(char));
    sprintf(label, "__L%ld", label_id++);
    return label;
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
        return;
    } case ND_BLOCK: {
        NodeVec_Member *member = node->body;
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
        generate_prologue(node->lhs->stack_frame_size);
        generate(node->lhs); // function body
        generate_epilogue();
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
