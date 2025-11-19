#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TOKEN_EOF,
    TOKEN_NUMBER,
    TOKEN_RESERVED,
} TokenType;

typedef enum {
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_NUM,
} NodeType;

typedef struct Token {
    TokenType type;
    struct Token *next;
    long value;
    char str[32];
    char *loc;
} Token;

typedef struct Node {
    NodeType type;     // Node type
    struct Node *lhs;  // Left-hand side
    struct Node *rhs;  // Right-hand side
    long val;          // Value (only for ND_NUM)
} Node;

Token *token;
char user_input[1024];

// Create new node (type != ND_NUM)
Node *new_node(NodeType type, Node *lhs, Node *rhs){
    Node *node = calloc(1, sizeof(Node));
    node->type = type;
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

// Create new node (type == ND_NUM)
Node *new_num_node(long val) {
    Node *node = calloc(1, sizeof(Node));
    node->type = ND_NUM;
    node->val = val;
    return node;
}

// Throw an error message and exit
void error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[Error]: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}

void error_at(char *loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int pos = loc - user_input;
    fprintf(stderr, "         %s\n", user_input);
    fprintf(stderr, "[Error]: ");
    fprintf(stderr, "%*s", pos, ""); // pos個の空白を出力
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}

// Consume a token if it matches the expected string
// Return true if matched, false otherwise
bool consume(const char *op) {
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        return false;
    }
    token = token->next;
    return true;
}

// Consume a token if it matches the expected string
// Otherwise, throw an error
void expect(const char *op) {
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        error_at(token->loc, "Expected '%s', but got '%s'", op, token->str);
    }
    token = token->next;
}

// Expect a number token and return its value
// Otherwise, throw an error
long expect_number() {
    if (token->type != TOKEN_NUMBER) {
        error_at(token->loc, "Expected a number, but got '%s'", token->str);
    }
    long val = token->value;
    token = token->next;
    return val;
}

Node *expr();
Node *mul();
Node *primary();
Node *unary();

Node *expr() {
    Node *node = mul();
    
    while (true) {
        if (consume("+")) {
            node = new_node(ND_ADD, node, mul());
        } else if (consume("-")) {
            node = new_node(ND_SUB, node, mul());
        } else {
            return node;
        }
    }
}

Node *mul() {
    Node *node = unary();

    while (true) {
        if (consume("*")) {
            node = new_node(ND_MUL, node, unary());
        } else {
            return node;
        }
    }
}

Node *primary() {       // primary = num | ("(" expr ")")
    if (consume("(")) { // かっこがあるなら、"(" expr ")"のはず
        Node *node = expr();

        expect(")"); // かっこは閉じられるはず...
        return node;
    } else {         // numの部分
        return new_num_node(expect_number());
    }
}

Node *unary() {
    if (consume("+")) {
        return new_node(ND_ADD, new_num_node(0), unary());
    } else if (consume("-")) {
        return new_node(ND_SUB, new_num_node(0), unary());
    } else {
        return primary();
    }
}

void generate(Node *node) {
    if(node->type == ND_NUM) {
        printf("ld %ld\n", node->val);
        return;
    }

    generate(node->lhs);
    generate(node->rhs);

    switch (node->type) {
    case ND_ADD:
        printf("add\n");
        break;
    case ND_SUB:
        printf("sub\n");
        break;
    case ND_MUL:
        printf("mul\n");
        break;
    default:
        error("Unknown node type");
        break;
    }
}

Token *new_token(TokenType type, Token *current, const char *str, long val, char *loc) {
    Token *tok = calloc(1, sizeof(Token));
    tok->type = type;
    if (str) {
        strncpy(tok->str, str, sizeof(tok->str) - 1);
    }
    tok->value = val;
    tok->loc = loc;
    current->next = tok;
    return tok;
}

// Check if the current token is EOF
bool at_eof() {
    return token->type == TOKEN_EOF;
}

Token *tokenize(const char *p){
    Token head;
    head.next = NULL;
    Token *cur = &head;

    while (*p) {
        // Skip whitespace
        if (isspace(*p)) {
            p++;
            continue;
        }

        if (*p == '+' || *p == '-' || *p == '*' || *p == '(' || *p == ')') {
            cur = new_token(TOKEN_RESERVED, cur, (char[]){*p, 0}, 0, (char *)p);
            p++;
            continue;
        }

        if (isdigit(*p)) {
            char *q = (char *)p;
            long val = strtol(p, &q, 0);
            if (val < 0 || val > 0xFF) {
                error_at((char *)p, "Number out of range");
            }
            cur = new_token(TOKEN_NUMBER, cur, NULL, val, (char *)p);
            p = q;
            continue;
        }

        error_at((char *)p, "Invalid token");
    }

    new_token(TOKEN_EOF, cur, NULL, 0, (char *)p);
    return head.next;
}

int main() {
    // if (argc != 2) {
    //     fprintf(stderr, "Usage : <code>\n");
    //     return EXIT_FAILURE;
    // }
    fgets(user_input, sizeof(user_input), stdin);
    char *cr_lf = strpbrk(user_input, "\r\n"); //改行コードを排除
    if (cr_lf) {
        *cr_lf = '\0';
    }

    token = tokenize(user_input);

    Node *node = expr();
    generate(node);

    return EXIT_SUCCESS;
}