#include "ast.h"
#include "nodes.h"

Node code[256];

extern Vars_List *head;
extern Vars_List *current;
extern Vars_List *tail;

// Create new node (type != ND_NUM)
void program() {
    long i = 0;
    head = calloc(1, sizeof(Vars_List));
    head->parent = NULL;
    head->var_alloc_ptr = 0x10; // グローバル変数のアドレスは0x10から割り当てる
    tail = head;
    while (!at_eof()) {
        code[i++] = *toplevel(token->loc);
    }
    code[i] = *new_node(ND_EOF, NULL, NULL, token->loc);

    for (long j = 0; j < i; j++) {
        print_node(&code[j]);
    }
    generate_top(code, i);
}

Node *toplevel(char *l) {
    char *loc = l;
    Type_t *type = calloc(1, sizeof(Type_t));
    type->size = -1;
    type->type = TY_INT;
    if (consume_la("uint8_t", &loc) || consume_la("int", &loc) || consume_la("char", &loc)) {
        type->size = 1;
    } else if (consume_la("void", &loc)) {
        type->size = 0;
    } else {
        error_at(loc, "Type specifier expected for global variable and function: %.*s", token->len, token->str);
    }
    long address = -1;
    if (consume_la("[", &loc)) {
        expect("[", &loc);
        char *attr = expect_ident(&loc);
        if (strcmp(attr, "address") == 0) {
            expect("=", &loc);
            address = expect_number( &loc);
            expect("]", &loc);
            expect("]", &loc);
        } else {
            error_at(loc, "Unknown attribute for global variable: %s", attr);
        }
    }

    Type_t *cur = type;
    while (consume_la("*", &loc)) {
        cur->type = TY_PTR;
        cur->size = PTR_SIZE;
        cur->ptr_to = calloc(1, sizeof(Type_t));
        cur = cur->ptr_to;
    }

    Token *tok = token;
    char *name = expect_ident(&loc);

    if (consume_la("(", &loc)) {
        Node **nv = calloc(1, sizeof(Node**));
        long arg_count = 0;
        new_scope();
        while (!consume(")", &loc)) {
            Node *arg = expr(loc);
            nv = nodevec_push(nv, arg_count++, arg);
            if (!consume(",", &loc)) {
                expect(")", &loc);
                break;
            }
        }
        add_function(tok);
        Node *node = new_func_node(ND_FUNC_DEF, name, nv, stmt(loc), arg_count, type, loc);
        end_scope();
        return node;
    } else {
        if (address != -1) {
            add_global_var(tok, address);
        } else {
            add_global_var(tok, head->var_alloc_ptr++);
        }
        if (consume_la("=", &loc)) {
            Node *rhs = assign(loc);
            expect(";", &loc);
            return new_node(ND_ASSIGN, new_ident_node(ND_GLOBAL_VAR, name, head->var_tail->address, type, loc), rhs, loc);
        }
        expect(";", &loc);
        return new_ident_node(ND_GLOBAL_VAR, name, head->var_tail->address, type, loc);
    }
}

Node *stmt(char *l) {
    Node *node;
    char *loc = l;
    if (consume_la("return", &loc)) {
        if (consume_la(";", &loc)) {
            return new_node(ND_RETURN, NULL, NULL, loc);
        }
        node = new_node(ND_RETURN, expr(loc), NULL, loc);
        expect(";", &loc);

    } else if (consume_la("if", &loc)) {
        expect("(", &loc);
        Node *cond = expr(loc);
        expect(")", &loc);
        Node *then = stmt(loc);
        Node *els = NULL;
        if (consume_la("else", &loc)) {
            els = stmt(loc);
        }
        if (els) {
            node = new_if_else_node(ND_IF_ELSE, cond, then, els, loc);
        } else {
            node = new_if_else_node(ND_IF, cond, then, NULL, loc);
        }
    } else if (consume_la("for", &loc)) {
        expect("(", &loc);
        Node *init = NULL;
        new_scope();
        if (!consume_la(";", &loc)) {
            init = expr(loc);
            expect(";", &loc);
        }
        Node *cond = NULL;
        if (!consume_la(";", &loc)) {
            cond = expr(loc);
            expect(";", &loc);
        }
        Node *inc = NULL;
        if (!consume_la(")", &loc)) {
            inc = expr(loc);
            expect(")", &loc);
        }
        Node *body = stmt(loc);
        end_scope();
        node = new_for_node(cond, inc, init, body, loc);
    } else if (consume_la("while", &loc)) {
        expect("(", &loc);
        Node *cond = expr(loc);
        expect(")", &loc);
        Node *body = stmt(loc);
        node = new_while_node(cond, body, loc);
    } else if (consume_la("break", &loc)){
        expect(";", &loc);
        node = new_node(ND_BREAK, NULL, NULL, loc);
    } else if (consume_la("{", &loc)) {
        new_scope();
        node = new_block_node();
        Node **nv = calloc(1, sizeof(Node**));
        if (!nv) error("Memory allocation failed");
        size_t len = 0;
        while (!consume("}", &loc)) {
            nv = nodevec_push(nv, len++, stmt(loc));
        }
        node->body = nv;
        node->arg_sf_size = end_scope();
    } else {
        node = expr(loc);
        expect(";", &loc);
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
    Node *node = bitwise_or(loc);

    if (consume_la("=", &loc)) {
        node = new_node(ND_ASSIGN, node, assign(loc), loc);
    }
    return node;
}

Node *bitwise_or(char *l) {
    char *loc = l;
    Node *node = bitwise_xor(loc);
    while (true) {
        if (consume_la("|", &loc)) {
            node = new_node(ND_BITWISE_OR, node, bitwise_xor(loc), loc);
        } else {
            return node;
        }
    }
}

Node *bitwise_xor(char *l) {
    char *loc = l;
    Node *node = bitwise_and(loc);
    while (true) {
        if (consume_la("^", &loc)) {
            node = new_node(ND_BITWISE_XOR, node, bitwise_and(loc), loc);
        } else {
            return node;
        }
    }
}

Node *bitwise_and(char *l) {
    char *loc = l;
    Node *node = equality(loc);
    while (true) {
        if (consume_la("&", &loc)) {
            node = new_node(ND_BITWISE_AND, node, equality(loc), loc);
        } else {
            return node;
        }
    }
}


Node *equality(char *l) {
    char *loc = l;
    Node *node = relational(loc);

    while (true) {
        if (consume_la("==", &loc)) {
            node = new_node(ND_EQ, node, relational(loc), loc);
        } else if (consume_la("!=", &loc)) {
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
        if       (consume_la("<=", &loc)) {
            node = new_node(ND_GE, add(loc), node, loc);
        } else if(consume_la(">=", &loc)) {
            node = new_node(ND_GE, node, add(loc), loc);
        } else if (consume_la(">", &loc)) {
            node = new_node(ND_LT, add(loc), node, loc);
        } else if (consume_la("<", &loc)) {
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
        if (consume_la("+", &loc)) {
            node = new_node(ND_ADD, node, mul(loc), loc);
        } else if (consume_la("-", &loc)) {
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
        if (consume_la("*", &loc)) {
            node = new_node(ND_MUL, node, unary(loc), loc);
        } else {
            return node;
        }
    }
}

Node *primary(char *l) {       // primary = num | ident | "(" expr ")"
    char *loc = l;
    if (consume_la("(", &loc)) { // かっこがあるなら、"(" expr ")"のはず
        Node *node = expr(loc);
        expect(")", &loc); // かっこは閉じられるはず...
        return node;
    } else if (is_number_token()) {         // numの部分
        return new_num_node(expect_number(&loc), loc);
    } else {                                // identの部分
        return ident(loc);
    }
}

Node *ident(char *l) {
    char *loc = l;
    Type_t *type = calloc(1, sizeof(Type_t));
    type->size = -1;
    type->type = TY_INT;
    if (consume_la("uint8_t", &loc) || consume_la("int", &loc) || consume_la("char", &loc)) {
        type->size = 1; // Currently uint8_t, int and char mean the same (1 byte int) type.
    } else if (consume_la("void", &loc)) {
        type->size = 0; // void type has size 0
    } else {
        // Considers reference to variable or function if there is no type specifier
    }
    Type_t *cur = type;
    while (consume_la("*", &loc)) {
        cur = calloc(1, sizeof(Type_t));
        if (!cur) {
            error("Memory allocation failed");
        }
        cur->type = TY_PTR;
        cur->size = PTR_SIZE;
        cur->ptr_to = type;
    }
    type = cur;
    Ident_Name *name = find_name(token);
    Token *tok = token;
    char *var_name = expect_ident(&loc);
    if (name && name->type == FUNCTION && consume_la("(", &loc)) { // ident "(" ((expr ",")* expr)? ")" の部分（関数呼び出し）
        Node **args = calloc(1, sizeof(Node**));
        if (!args) {
            error("Memory allocation failed");
        }
        size_t argnum = 0;
        while (!consume(")", &loc)) {
            args = nodevec_push(args, argnum++, expr(loc));
            if (!consume(",", &loc)) {
                expect(")", &loc);
                break;
            }
        }
        return new_func_node(ND_FUNC_CALL, var_name, args, NULL, (long) argnum, type, loc);
    }
    if (type->size == -1 && name) {
        if (name->type == VAR_GLOBAL_STATIC) {
            fprintf(stderr, "Found global variable: %.*s at address %ld\n", (int)tok->len, tok->str, name->address);
            return new_ident_node(ND_GLOBAL_VAR, var_name, name->address, type, loc);
        } else if (name->type == VAR_LOCAL) {
            fprintf(stderr, "Found variable: %.*s at offset %ld\n", (int)tok->len, tok->str, name->offset);
            return new_ident_node(ND_LOCAL_VAR, var_name, name->offset, type, loc);
        }
    } else if (type->size > 0) {
        add_local_var(tok);
        fprintf(stderr, "Added local variable: %.*s at offset %ld\n", (int)tok->len, tok->str, current->var_tail->offset);
        return new_ident_node(ND_LOCAL_VAR, var_name, current->var_tail->offset, type, loc);
    }
    error_at(loc, "Undefined or invalid variable: %.*s", (int)tok->len, tok->str);
}

Node *unary(char *l) {
    char *loc = l;
    if (consume_la("+", &loc)) {
        return primary(loc);
    } else if (consume_la("-", &loc)) {
        return new_node(ND_SUB, new_num_node(0, loc), primary(loc), loc);
    } else if (consume_la("~", &loc)){
        return new_node(ND_BITWISE_XOR, new_num_node(0xFF, loc), primary(loc), loc);
    } else if (consume_la("&", &loc)){
        return new_node(ND_ADDR, unary(loc), NULL, loc);
    } else if (consume_la("*", &loc)){
        return new_node(ND_DEREF, unary(loc), NULL, loc);
    } else {
        return primary(loc);
    }
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
        long cur_max_vars_count = count_local_vars();
        if (tail->max_var_count < cur_max_vars_count) {
            tail->max_var_count = cur_max_vars_count;
        }
        if (tail->parent->max_var_count < tail->max_var_count) {
            tail->parent->max_var_count = tail->max_var_count;
        }
        Vars_List *old_tail = tail;
        tail = tail->parent;
        free_vars_list(old_tail);
        tail->child = NULL;
        current = tail;
    } else {
        error("No scope to end");
    }
    return tail->max_var_count;
}
