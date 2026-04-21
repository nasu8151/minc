#ifndef __MINCC_NODES_H__
#define __MINCC_NODES_H__

#include <stdio.h>
#include "errorhandle.h"

#define PTR_SIZE 2

typedef enum {
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_EQ,
    ND_NEQ,
    ND_LT,
    ND_GE,
    ND_BITWISE_OR,
    ND_BITWISE_AND,
    ND_BITWISE_XOR,
    ND_BITWISE_NOT,
    ND_NUM,
    ND_LOCAL_VAR,
    ND_GLOBAL_VAR,
    ND_ASSIGN,
    ND_RETURN,
    ND_IF,
    ND_IF_ELSE,
    ND_FOR,
    ND_WHILE,
    ND_BLOCK,
    ND_FUNC_DEF,
    ND_FUNC_CALL,
    ND_ATTR,
    ND_BREAK,
    ND_ADDR,
    ND_DEREF,

    ND_EOF
} NodeType;

struct NodeList_Member;

typedef struct Type_t{
    enum {TY_INT, TY_PTR} type;
    long size;
    struct Type_t *ptr_to;
} Type_t;

typedef struct Node {
    NodeType type;      // Node type
    struct Node *lhs;   // Left-hand side  ('then' in IF, IF_ELSE, 'body' in FOR, WHILE)
    struct Node *rhs;   // Right-hand side
    struct Node *else_; // Else branch (for IF_ELSE statements)
    struct Node *cond;  // Condition (for IF, WHILE, FOR statements)
    struct Node *inc;   // Increment (for FOR statement)
    struct Node *init;  // Initialization (for FOR statement)
    struct Node **body; // Block body (for BLOCK, FUNC_DEF statements)
    long val;           // Value (only for ND_NUM)
    struct Type_t *valtype;        // Type (only for ND_GLOBAL_VALUE and ND_LOCAL_VALUE)
    long ofs_addr;        // Offset from BP or Absolute address (only for ND_LOCAL_VAR, ND_GLOBAL_VAR)
    long arg_sf_size; // Stack frame size (only for ND_BLOCK used in FUNC_DEF) or number of arguments (only for ND_FUNC_DEF)
    unsigned long name_len; // Length of identifier name
    char *name;    // Identifier name (for ND_LOCAL_VAR, ND_GLOBAL_VAR, ND_FUNC_DEF, ND_FUNC_CALL)
    char *loc;
} Node;

typedef enum {
    VAR_LOCAL,
    VAR_GLOBAL_STATIC,
    FUNCTION,
} IdentType;

typedef struct Ident_Name {
    struct Ident_Name *next;
    unsigned long name_len; // Length of variable name
    char *name;       // Variable name (null-terminated)
    IdentType type;     // Variable type
    long address;     // Address for global variables
    long offset;      // Offset from BP or base address
    Type_t *valtype;  // Variable type
} Ident_Name;

typedef enum {
    TOKEN_EOF,
    TOKEN_NUMBER,
    TOKEN_RESERVED,
    TOKEN_IDENT,
} TokenType;

typedef struct Token {
    TokenType type;
    struct Token *next;
    long value;
    unsigned long len; // Token size
    char *str;       // Token string (must be null-terminated)
    char *loc;
} Token;

typedef struct Vars_List {
    struct Vars_List *parent;
    struct Vars_List *child;
    Ident_Name *var_head;
    Ident_Name *var_tail;
    long var_alloc_ptr;
    long max_var_count;
} Vars_List;


// Generate node
Node *new_node(NodeType type, Node *lhs, Node *rhs, char *loc);
Node *new_num_node(long val, char *loc);
Node *new_ident_node(NodeType type, char *name, long ofs_addr, Type_t *valtype, char *loc);
Node *new_if_else_node(NodeType type, Node *cond, Node *then, Node *else_, char *loc);
Node *new_for_node(Node *cond, Node *inc, Node *init, Node *body, char *loc);
Node *new_while_node(Node *cond, Node *body, char *loc);
Node *new_func_node(NodeType type, char *name, Node **args, Node *body, long arg_sf_size, Type_t *rettype, char *loc);
Node *new_block_node(char *loc);

Node **nodevec_push(Node **old_vec, size_t old_len, Node *node);

void print_node(Node *node);

void add_local_var(Token *tok, Type_t *type);
// Add global variable to global scope
void add_global_var(Token *tok, long address, Type_t *type);
// Add function to function list
void add_function(Token *tok, Type_t *type);
// Count local variables from current funciton scope
long count_local_vars();

Ident_Name *find_name(Token *tok);

void free_vars_list(Vars_List *vars_list);

#endif // __MINCC_NODES_H__