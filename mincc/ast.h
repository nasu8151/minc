#ifndef MINCC_AST_H
#define MINCC_AST_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "parse.h"
#include "errorhandle.h"
#include "sharedtype.h"
#include "codegen.h"

/*
global variable list structure
(top)
+ global_vars -> Ident_Name *next
                    -> ...
+ Vars_list *next (local vars)
    + local_vars -> Ident_Name *next
                    -> ...
    + Vars_list *next (block scope vars)
    + block_vars -> Ident_Name *next
                    -> ...
        + ...
*/

typedef struct Vars_List {
    struct Vars_List *parent;
    struct Vars_List *child;
    Ident_Name *var_head;
    Ident_Name *var_tail;
    long max_vars_count;
} Vars_List;


// Genelate node
Node *new_node(NodeType type, Node *lhs, Node *rhs, char *loc);
Node *new_num_node(long val, char *loc);
Node *new_ident_node(NodeType type, char *name, long offset, char *loc);
Node *new_if_else_node(NodeType type, Node *cond, Node *then, Node *else_, char *loc);
Node *new_for_node(Node *cond, Node *inc, Node *init, Node *body, char *loc);
Node *new_while_node(Node *cond, Node *body, char *loc);

// Syntax tree parsing functions
void program();
Node *toplevel(char *l);
Node *stmt(char *l);
Node *assign(char *l);
Node *equality(char *l);
Node *relational(char *l);
Node *expr(char *l);
Node *add(char *l);
Node *mul(char *l);
Node *primary(char *l);
Node *unary(char *l);

// Find local or global variable by Token
Ident_Name *find_name(Token *tok);
// Find function by Token
Ident_Name *find_function(Token *tok);
// Add local variable to current scope
void add_local_var(Token *tok);
// Add global variable to global scope
void add_global_var(Token *tok);
// Count local variables from current funciton scope
long count_local_vars();
// Count global variables
long count_global_vars();

void new_scope();
long end_scope();
void print_node(Node *node);
void free_vars_list(Vars_List *vars_list);

#endif // MINCC_AST_H