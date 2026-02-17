#include "ast.h"

Node code[256];

Vars_List *head = NULL;
Vars_List *current = NULL;
Vars_List *tail = NULL;

Ident_Name *func_head = NULL;
Ident_Name *func_tail = NULL;

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

Node *new_func_node(NodeType type, char *name, NodeList_Member *args, Node *body, long arg_sf_size, char *loc) {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = type;
    node->name = mystrndup(name, strlen(name));
    node->name_len = strlen(name);
    node->body = args;
    node->lhs = body;
    node->arg_sf_size = arg_sf_size;
    node->loc = loc;
    return node;
}

Node *new_node_list() {
    Node *node = calloc(1, sizeof(Node));
    if (!node) {
        error("Memory allocation failed");
    }
    node->type = ND_BLOCK;
    node->body = NULL;
    return node;
}

NodeList_Member *add_node_list(Node *node) {
    NodeList_Member *member = calloc(1, sizeof(NodeList_Member));
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
    } else if (node->type == ND_FUNC_DEF) {
        fprintf(stderr, "ND_FUNC_DEF: %s with stack frame size %ld\n", node->name, node->arg_sf_size);
        fprintf(stderr, "(BODY:\n");
        print_node(node->lhs);
        fprintf(stderr, ")\n");
    } else if (node->type == ND_FUNC_CALL) {
        fprintf(stderr, "ND_FUNC_CALL\n");
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
            NodeList_Member *cur = node->body;
            while (cur) {
                print_node(cur->node);
                cur = cur->next;
            }
            fprintf(stderr, ")\n");
        }
    }
}

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
    } else if (consume_la("{", &loc)) {
        new_scope();
        node = new_node_list();
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
    Node *node = equality(loc);

    if (consume_la("=", &loc)) {
        node = new_node(ND_ASSIGN, node, assign(loc), loc);
    }
    return node;
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
    } else {
        return primary(loc);
    }
}

Ident_Name *find_name(Token *tok) {
    Vars_List *cur = tail;
    while (cur) {
        Ident_Name *var = cur->var_head;
        while (var) {
            fprintf(stderr, "Comparing %s with %s\n", var->name, tok->str);
            if (var->name_len == tok->len && strncmp(var->name, tok->str, tok->len) == 0) {
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
    fprintf(stderr, "Adding local variable: %.*s\n", (int)tok->len, tok->str);
    var->name_len = tok->len;
    var->name = mystrndup(tok->str, tok->len);
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
    cur->max_var_count++;
    var->offset = 0 - count_local_vars(); // スタック上のオフセットを設定
}

void add_global_var(Token *tok, long address) {
    Ident_Name *var = calloc(1, sizeof(Ident_Name));
    if (!var) {
        error("Memory allocation failed");
    }
    var->name_len = tok->len;
    var->name = mystrndup(tok->str, tok->len);
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
    head->max_var_count++;
    var->address = address; // グローバル変数のアドレスを設定
    fprintf(stderr, "Adding global variable: %.*s at address %ld\n", (int)tok->len, tok->str, var->address);
}

void add_function(Token *tok) {
    Ident_Name *var = calloc(1, sizeof(Ident_Name));
    if (!var) {
        error("Memory allocation failed");
    }
    var->name_len = tok->len;
    var->name = mystrndup(tok->str, tok->len);
    var->type = FUNCTION;
    var->next = NULL;
    Ident_Name *gvar = head->var_head;
    if (!gvar) {
        head->var_head = var;
        head->var_tail = var;
    } else {
        head->var_tail->next = var;
        head->var_tail = var;
    }
    var->offset = 0;  // 関数のオフセットは0に設定
    var->address = 0; // 関数のアドレスは0に設定
    fprintf(stderr, "Adding function: %.*s \n", (int)tok->len, tok->str);
}

Ident_Name *find_function(Token *tok) {
    Ident_Name *cur = head->var_head;
    while (cur) {
        fprintf(stderr, "Comparing function %s with %s\n", cur->name, tok->str);
        if (cur->name_len == tok->len && strncmp(cur->name, tok->str, tok->len) == 0) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
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

void free_vars_list(Vars_List *list) {
    if (!list) return;
    Ident_Name *var = list->var_head;
    while (var) {
        Ident_Name *next_var = var->next;
        free(var->name);
        free(var);
        var = next_var;
    }
    free(list);
}
