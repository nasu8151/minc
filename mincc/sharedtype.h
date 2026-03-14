#ifndef MINCC_SHAREDTYPE_H
#define MINCC_SHAREDTYPE_H


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

    ND_EOF
} NodeType;


struct NodeList_Member;

typedef struct Node {
    NodeType type;      // Node type
    struct Node *lhs;   // Left-hand side  ('then' in IF, IF_ELSE, 'body' in FOR, WHILE)
    struct Node *rhs;   // Right-hand side
    struct Node *else_; // Else branch (for IF_ELSE statements)
    struct Node *cond;  // Condition (for IF, WHILE, FOR statements)
    struct Node *inc;   // Increment (for FOR statement)
    struct Node *init;  // Initialization (for FOR statement)
    struct NodeList_Member *body; // Block body (for BLOCK, FUNC_DEF statements)
    long val;           // Value (only for ND_NUM) or size of type (only for ND_GLOBAL_VAR and ND_LOCAL_VAR)
    long ofs_addr;        // Offset from BP or Absolute address (only for ND_LOCAL_VAR, ND_GLOBAL_VAR)
    long arg_sf_size; // Stack frame size (only for ND_BLOCK used in FUNC_DEF) or number of arguments (only for ND_FUNC_DEF)
    unsigned long name_len; // Length of identifier name
    char *name;    // Identifier name (for ND_LOCAL_VAR, ND_GLOBAL_VAR, ND_FUNC_DEF, ND_FUNC_CALL)
    char *loc;
} Node;

typedef struct NodeList_Member {
    struct NodeList_Member *next;
    Node *node;
} NodeList_Member;

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

#endif // MINCC_SHAREDTYPE_H