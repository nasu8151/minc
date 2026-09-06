#include "ast.h"
#include "nodes.h"

Node code[256];

static char *isr_vector_owner[4]; // name of the function claiming each [[isr=N]] vector slot, NULL if unclaimed

extern Vars_List *head;
extern Vars_List *current;
extern Vars_List *tail;

// Create new node (type != ND_NUM)
long program() {
    long i = 0;
    head = calloc(1, sizeof(Vars_List));
    head->parent = NULL;
    head->var_alloc_ptr = 0x100; // グローバル変数のアドレスは0x10から割り当てる
    tail = head;
    while (!at_eof()) {
        code[i++] = *toplevel(token->loc);
    }
    code[i] = *new_node(ND_EOF, NULL, NULL, token->loc);

    for (long j = 0; j < i; j++) {
        print_node(&code[j]);
    }
    return i;
}

Node *toplevel(char *l) {
    char *loc = l;
    Type_t *type = check_type(&loc);
    long address = -1;
    long isr_vector = -1;
    if (consume_la("[", &loc)) {
        expect("[", &loc);
        do {
            char *attr = expect_ident(&loc);
            if (strcmp(attr, "address") == 0) {
                expect("=", &loc);
                address = expect_number( &loc);
            } else if (strcmp(attr, "isr") == 0) {
                if (type->size != 0) {
                    warn_at(loc, "Since ISRs doesn't have a return value, the type is ignored.");
                }
                type->type = TY_ISR;
                type->size = 0;
                if (consume_la("=", &loc)) {
                    isr_vector = expect_number(&loc);
                    if (isr_vector < 0 || isr_vector > 3) {
                        error_at(loc, "ISR vector number must be between 0 and 3, got %ld", isr_vector);
                    }
                }
            } else {
                error_at(loc, "Unknown attribute for global variable: %s", attr);
            }
        } while (consume(",", &loc));
        expect("]", &loc);
        expect("]", &loc);
    }

    Token *tok = token;
    char *name = expect_ident(&loc);

    if (consume_la("(", &loc)) {
        Node **nv = calloc(1, sizeof(Node**));
        long arg_count = 0;
        long arg_reg_count = 0;
        new_scope();
        while (!consume(")", &loc)) {
            Type_t *arg_type = check_type(&loc);
            if (arg_type) { // declaration detected
                Token *tok = token;
                char *arg_name = expect_ident(&loc);
                add_local_var(tok, arg_type);
                Node *arg = new_ident_node(ND_LOCAL_VAR, arg_name, current->var_tail->offset, arg_type, loc);
                if (!arg_name) {
                    error_at(loc, "no name for a variable");
                }
                int arg_size = (arg && arg->valtype) ? arg->valtype->size : 2;
                if (arg_size != 1 && arg_size != 2) {
                    error_at(loc, "Invalid argument size: %d", arg_size);
                }
                arg_reg_count += arg_size;
                nv = nodevec_push(nv, arg_count++, arg);
            }
            if (!consume(",", &loc)) {
                expect(")", &loc);
                break;
            }
        }
        if (type->type == TY_ISR && arg_count > 0) {
            error_at(loc, "ISR function '%s' cannot have parameters", name);
        }
        if (type->type == TY_ISR && isr_vector != -1) {
            if (isr_vector_owner[isr_vector]) {
                error_at(loc, "ISR vector %ld already claimed by '%s'", isr_vector, isr_vector_owner[isr_vector]);
            }
            isr_vector_owner[isr_vector] = name;
        }
        add_function(tok, type);
        expect("{", &loc);
        Node *node = new_func_node(ND_FUNC_DEF, name, nv, block(loc), arg_reg_count, type, loc);
        node->isr_vector = isr_vector;
        node->lhs->arg_sf_size = end_scope();
        return node;
    } else {
        if (address != -1) {
            add_global_var(tok, address, type);
        } else {
            add_global_var(tok, head->var_alloc_ptr++, type);
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

Node *block(char *l) {
    Node *node;
    char *loc = l;
    node = new_block_node(loc);
    Node **nv = calloc(1, sizeof(Node**));
    if (!nv) error("Memory allocation failed");
    size_t len = 0;
    while (!consume("}", &loc)) {
        Node *n = decr(loc);
        if (n) {
            nv = nodevec_push(nv, len++, n);
        } else {
            nv = nodevec_push(nv, len++, stmt(loc));
        }
    }
    node->body = nv;
    return node;
}

// [NOTE] if it didnt detect declaration, return NULL!!
Node *decr(char *l) {
    Node *node;
    char *loc = l;
    Type_t *type = check_type(&loc);
    if (type) { // declaration detected
        Token *tok = token;
        char *var_name = expect_ident(&loc);
        if (!var_name) {
            error_at(loc, "no name for a variable");
        }
        add_local_var(tok, type);
        fprintf(stderr, "Added local variable: %.*s at offset %ld\n", (int)tok->len, tok->str, current->var_tail->offset);
        node = new_ident_node(ND_LOCAL_VAR, var_name, current->var_tail->offset, type, loc);
        if (consume_la("=", &loc)) {
            Node *rhs = expr(loc);
            expect(";", &loc);
            return new_node(ND_ASSIGN, node, rhs, loc);
        }
        expect(";", &loc);
        return node;
    } else {
        return NULL;
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
            init = decr(loc);
            if (!init) {
                init = expr(loc);
                expect(";", &loc);
            }
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
    } else if (consume_la("asm", &loc)) {
        expect("(", &loc);
        // Adjacent string literals concatenate, exactly like in C, so a
        // multi-instruction block can be written one instruction per line.
        char *text = expect_string(&loc);
        size_t len = strlen(text);
        char *joined = mystrndup(text, len);
        while (is_string_token()) {
            char *more = expect_string(&loc);
            size_t more_len = strlen(more);
            char *grown = realloc(joined, len + more_len + 1);
            if (!grown) {
                error("Memory allocation failed");
            }
            joined = grown;
            memcpy(joined + len, more, more_len + 1);
            len += more_len;
        }
        expect(")", &loc);
        expect(";", &loc);
        node = new_asm_node(joined, loc);
    } else if (consume_la("{", &loc)) {
        new_scope();
        node = block(loc);
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
    Node *node = and(loc);
    while (true) {
        if (consume_la("&", &loc)) {
            node = new_node(ND_BITWISE_AND, node, and(loc), loc);
        } else {
            return node;
        }
    }
}

Node *and(char *l) {
    char *loc = l;
    Node *node = or(loc);
    while (true) {
        if (consume_la("&&", &loc)) {
            node = new_node(ND_AND, node, or(loc), loc);
        } else {
            return node;
        }
    }
}

Node *or(char *l) {
    char *loc = l;
    Node *node = equality(loc);
    while (true) {
        if (consume_la("||", &loc)) {
            node = new_node(ND_OR, node, equality(loc), loc);
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

// sei()/cli(): flip just the IE bit (bit1) of PSR, which lives at data address
// 0x0002 and is only reachable through stm/ldm -- the ISA's stf/clf mnemonics
// are decoded by neither minc_h.sv nor the pipelined cores. Read-modify-write
// rather than a blunt store so PSR bit0 (the carry flag) survives: the value
// read back in r0 carries the old bit0 and is written straight back out.
// r0/r1 are dead at every statement boundary (an ISR prologue saves both
// unconditionally), so no caller-saved bookkeeping is needed here.
#define SEI_ASM "ldm r0,2\nmvi r1,2\nor r0,r1\nstm 2,r0\n"
#define CLI_ASM "ldm r0,2\nmvi r1,253\nand r0,r1\nstm 2,r0\n"

Node *ident(char *l) {
    char *loc = l;
    Ident_Name *name = find_name(token);
    Token *tok = token;
    // Builtins. Looked up only when nothing else claims the name, so a user
    // declaration of `sei`/`cli` still shadows them.
    if (!name && tok->type == TOKEN_IDENT) {
        const char *builtin = NULL;
        if (strcmp(tok->str, "sei") == 0) {
            builtin = SEI_ASM;
        } else if (strcmp(tok->str, "cli") == 0) {
            builtin = CLI_ASM;
        }
        if (builtin) {
            expect_ident(&loc);
            expect("(", &loc);
            expect(")", &loc);
            return new_asm_node(mystrndup(builtin, strlen(builtin)), loc);
        }
    }
    char *var_name = expect_ident(&loc);
    if (name && name->type == FUNCTION && consume_la("(", &loc)) { // ident "(" ((expr ",")* expr)? ")" の部分（関数呼び出し）
        if (name->valtype && name->valtype->type == TY_ISR) {
            error_at(loc, "ISR function '%.*s' cannot be called directly", (int)tok->len, tok->str);
        }
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
        return new_func_node(ND_FUNC_CALL, var_name, args, NULL, (long) argnum, name->valtype, loc);
    }
    if (name) {
        if (name->type == VAR_GLOBAL_STATIC) {
            fprintf(stderr, "Found global variable: %.*s at address %ld\n", (int)tok->len, tok->str, name->address);
            return new_ident_node(ND_GLOBAL_VAR, var_name, name->address, name->valtype, loc);
        } else if (name->type == VAR_LOCAL) {
            fprintf(stderr, "Found variable: %.*s at offset %ld\n", (int)tok->len, tok->str, name->offset);
            return new_ident_node(ND_LOCAL_VAR, var_name, name->offset, name->valtype, loc);
        }
    }
    error_at(loc, "Undefined or invalid identifyer: %.*s", (int)tok->len, tok->str);
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
        Node *operand = unary(loc);

        if (!operand || !operand->valtype || operand->valtype->type != TY_PTR) {
            error_at(loc, "Cannot dereference non-pointer type");
        }

        Node *node = new_node(ND_DEREF, operand, NULL, loc);
        node->valtype = operand->valtype->ptr_to;
        return node;
    } else if (consume_la("!", &loc)) {
        return new_node(ND_NOT, unary(loc), NULL, loc);
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
        long cur_max_vars_bytes = sizeof_local_vars();
        if (tail->max_var_bytes < cur_max_vars_bytes) {
            tail->max_var_bytes = cur_max_vars_bytes;
        }
        if (tail->parent->max_var_bytes < tail->max_var_bytes) {
            tail->parent->max_var_bytes = tail->max_var_bytes;
        }
        Vars_List *old_tail = tail;
        tail = tail->parent;
        free_vars_list(old_tail);
        tail->child = NULL;
        current = tail;
    } else {
        error("No scope to end");
    }
    return tail->max_var_bytes;
}
