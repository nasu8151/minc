char *mystrndup(const char *s, size_t n);

typedef enum {
    TOKEN_EOF,
    TOKEN_NUMBER,
    TOKEN_RESERVED,
    TOKEN_IDENT,
} TokenType;

typedef enum {
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_EQ,
    ND_NEQ,
    ND_LT,
    ND_LE,
    ND_GT,
    ND_GE,
    ND_NUM,
    ND_LOC_VAR,
    ND_ASSIGN,
    ND_RETURN,

    ND_EOF
} NodeType;

typedef struct Token {
    TokenType type;
    struct Token *next;
    long value;
    unsigned long size; // Token size
    char *str;       // Token string (must be null-terminated)
    char *loc;
} Token;

typedef struct Node {
    NodeType type;     // Node type
    struct Node *lhs;  // Left-hand side
    struct Node *rhs;  // Right-hand side
    long val;          // Value (only for ND_NUM)
    long offset;       // Offset from BP (only for ND_LOC_VAR)
    unsigned long name_len; // Length of identifier name
    char *name;    // Identifier name (only for ND_LOC_VAR)
    char *loc;
} Node;

typedef struct LocalVar {
    struct LocalVar *next;
    unsigned long name_len; // Length of variable name
    char *name;       // Variable name (null-terminated)
    long offset;      // Offset from BP
} LocalVar;

extern Token *token;

// Tokenizer functions
// Create a new token and link it to the current token
Token *new_token(TokenType type, Token *current, const char *str, unsigned long size, long val, char *loc);

// Tokenize the input string and return the head of the token list
Token *tokenize(const char *p);

// Token consumption functions
bool consume(const char *op, char *loc);
void expect(const char *op, char *loc);
long expect_number(char *loc);
char *expect_ident(char *loc);
bool is_number_node();
bool at_eof();

// Genelate node
Node *new_node(NodeType type, Node *lhs, Node *rhs, char *loc);
Node *new_num_node(long val, char *loc);
Node *new_ident_node(char *name, long offset, char *loc);

// Syntax tree parsing functions
void program();
Node *stmt(char *l);
Node *assign(char *l);
Node *equality(char *l);
Node *relational(char *l);
Node *expr(char *l);
Node *add(char *l);
Node *mul(char *l);
Node *primary(char *l);
Node *unary(char *l);

// Local variable functions
LocalVar *find_local_var(Token *tok);
void add_local_var(Token *tok);
long count_local_vars();

// node genelator function
void generate(Node *node);
void generate_prologue(long local_var_count);

// Error handling functions
void error(const char *fmt, ...);
void error_at(char *loc, const char *fmt, ...);

void warn(const char *fmt, ...);
void warn_at(char *loc, const char *fmt, ...);
