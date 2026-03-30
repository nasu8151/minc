#include "nodes.h"

Vars_List *head = NULL;
Vars_List *current = NULL;
Vars_List *tail = NULL;

Ident_Name *func_head = NULL;
Ident_Name *func_tail = NULL;

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

Node *new_block_node() {
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
