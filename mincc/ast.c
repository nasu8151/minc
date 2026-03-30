#include "ast.h"
#include "nodes.h"

Node code[256];

extern Vars_List *head;
extern Vars_List *current;
extern Vars_List *tail;

extern Ident_Name *func_head;
extern Ident_Name *func_tail;

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
    if (consume_la("uint8_t", &loc) || consume_la("int", &loc) || consume_la("char", &loc)) {
        // Currently only uint8_t and int types are supported for global variables
    } else if (consume_la("void", &loc)) {
        // void type is supported for function return type
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

    Token *tok = token;
    char *name = expect_ident(&loc);

    if (consume_la("(", &loc)) {
        NodeList_Member *nl = calloc(1, sizeof(NodeList_Member));
        if (!nl) {
            error("Memory allocation failed");
        }
        NodeList_Member *nl_head = nl;
        long arg_count = 0;
        new_scope();
        while (!consume(")", &loc)) {
            arg_count++;
            Node *arg = expr(loc);
            nl->next = add_node_list(arg);
            nl = nl->next;
            if (!consume(",", &loc)) {
                expect(")", &loc);
                break;
            }
        }
        add_function(tok);
        Node *node = new_func_node(ND_FUNC_DEF, name, nl_head->next, stmt(loc), arg_count, loc);
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
            return new_node(ND_ASSIGN, new_ident_node(ND_GLOBAL_VAR, name, head->var_tail->address, loc), rhs, loc);
        }
        expect(";", &loc);
        return new_ident_node(ND_GLOBAL_VAR, name, head->var_tail->address, loc);
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
        NodeList_Member *head = calloc(1, sizeof(NodeList_Member));
        if (!head) {
            error("Memory allocation failed");
        }
        NodeList_Member *cur = head;
        while (!consume("}", &loc)) {
            cur->next = add_node_list(stmt(loc));
            cur = cur->next;
        }
        node->body = head->next;
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
    } else {                               // identの部分
        long size = -1;
        if (consume_la("uint8_t", &loc) || consume_la("int", &loc) || consume_la("char", &loc)) {
            size = 1; // Currently uint8_t, int and char mean the same (1 byte int) type.
        } else if (consume_la("void", &loc)) {
            size = 0; // void type has size 0
        } else {
            // Considers reference to variable or function if there is no type specifier
        }
        Ident_Name *name = find_name(token);
        Token *tok = token;
        char *var_name = expect_ident(&loc);
        if (name && name->type == FUNCTION && consume_la("(", &loc)) { // ident "(" ((expr ",")* expr)? ")" の部分（関数呼び出し）
            NodeList_Member *args = calloc(1, sizeof(NodeList_Member));
            if (!args) {
                error("Memory allocation failed");
            }
            NodeList_Member *args_head = args;
            while (!consume(")", &loc)) {
                args->next = add_node_list(expr(loc));
                args = args->next;
                if (!consume(",", &loc)) {
                    expect(")", &loc);
                    break;
                }
            }
            return new_func_node(ND_FUNC_CALL, var_name, args_head->next, NULL, 0, loc);
        }
        if (size == -1 && name) {
            if (name->type == VAR_GLOBAL_STATIC) {
                fprintf(stderr, "Found global variable: %.*s at address %ld\n", (int)tok->len, tok->str, name->address);
                return new_ident_node(ND_GLOBAL_VAR, var_name, name->address, loc);
            } else if (name->type == VAR_LOCAL) {
                fprintf(stderr, "Found variable: %.*s at offset %ld\n", (int)tok->len, tok->str, name->offset);
                return new_ident_node(ND_LOCAL_VAR, var_name, name->offset, loc);
            }
        } else if (size > 0) {
            add_local_var(tok);
            fprintf(stderr, "Added local variable: %.*s at offset %ld\n", (int)tok->len, tok->str, current->var_tail->offset);
            return new_ident_node(ND_LOCAL_VAR, var_name, current->var_tail->offset, loc);
        }
        error_at(loc, "Undefined or invalid variable: %.*s", (int)tok->len, tok->str);
    }
}

Node *unary(char *l) {
    char *loc = l;
    if (consume_la("+", &loc)) {
        return new_node(ND_ADD, new_num_node(0, loc), unary(loc), loc);
    } else if (consume_la("-", &loc)) {
        return new_node(ND_SUB, new_num_node(0, loc), unary(loc), loc);
    } else if (consume_la("~", &loc)){
        return new_node(ND_BITWISE_XOR, new_num_node(0xFF, loc), unary(loc), loc);
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
