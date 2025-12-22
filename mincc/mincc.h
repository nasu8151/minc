typedef enum {
    TOKEN_EOF,
    TOKEN_NUMBER,
    TOKEN_RESERVED,
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
} NodeType;

typedef struct Token {
    TokenType type;
    struct Token *next;
    long value;
    char str[32];       // Token string (must be null-terminated)
    char *loc;
} Token;

typedef struct Node {
    NodeType type;     // Node type
    struct Node *lhs;  // Left-hand side
    struct Node *rhs;  // Right-hand side
    long val;          // Value (only for ND_NUM)
} Node;

extern Token *token;

// Tokenizer functions
// Create a new token and link it to the current token
Token *new_token(TokenType type, Token *current, const char *str, unsigned long size, long val, char *loc);

// Tokenize the input string and return the head of the token list
Token *tokenize(const char *p);

// Token consumption functions
bool consume(const char *op);
void expect(const char *op);
long expect_number();
bool at_eof();

// Genelate node
Node *new_node(NodeType type, Node *lhs, Node *rhs);
Node *new_num_node(long val);

// Syntax tree parsing functions
Node *equality();
Node *relational();
Node *expr();
Node *add();
Node *mul();
Node *primary();
Node *unary();

// node genelator function
void generate(Node *node);

// Error handling functions
void error(const char *fmt, ...);
void error_at(char *loc, const char *fmt, ...);

void warn(const char *fmt, ...);
void warn_at(char *loc, const char *fmt, ...);
