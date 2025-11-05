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

typedef struct Token {
    TokenType type;
    struct Token *next;
    long value;
    char str[32];
} Token;

Token *token;

// Throw an error message and exit
void error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
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
        error("Expected '%s'", op);
    }
    token = token->next;
}

// Expect a number token and return its value
// Otherwise, throw an error
long expect_number() {
    if (token->type != TOKEN_NUMBER) {
        error("Expected a number");
    }
    long val = token->value;
    token = token->next;
    return val;
}

Token *new_token(TokenType type, Token *current, const char *str, long val) {
    Token *tok = calloc(1, sizeof(Token));
    tok->type = type;
    if (str) {
        strncpy(tok->str, str, sizeof(tok->str) - 1);
    }
    tok->value = val;
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

        if (*p == '+' ) {
            cur = new_token(TOKEN_RESERVED, cur, "+", 0);
            p++;
            continue;
        }

        if (isdigit(*p)) {
            char *q = (char *)p;
            long val = strtol(p, &q, 0);
            if (val < 0 || val > 0xFF) {
                error("Number out of range: %s", p);
            }
            cur = new_token(TOKEN_NUMBER, cur, NULL, val);
            p = q;
            continue;
        }

        error("Invalid token: %s", p);
    }

    new_token(TOKEN_EOF, cur, NULL, 0);
    return head.next;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage : <code>\n");
        return EXIT_FAILURE;
    }

    token = tokenize(argv[1]);
    
    printf("ld %ld\n", expect_number());
    while (!at_eof())
    {
        if (consume("+")) {
            printf("add %ld\n", expect_number());
        } else {
            error("Invalid token");
        }
    }

    return EXIT_SUCCESS;
}