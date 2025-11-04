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

// Check if the current token is EOF
bool at_eof() {
    return token->type == TOKEN_EOF;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage : <code>\n");
        return EXIT_FAILURE;
    }

    const char *code = argv[1];
    long opr = strtol(code, &code, 0);
    if (opr < 0 || opr > 0xFF) {
        fprintf(stderr, "Operand out of range: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    printf("ld %ld\n", opr);
    while (*code) {
        if (*code == '+') {
            code++;
            opr = strtol(code, &code, 0);
            if (opr < 0 || opr > 0xFF) {
                fprintf(stderr, "Operand out of range: %s\n", argv[1]);
                return EXIT_FAILURE;
            }
            printf("add %ld\n", opr);
        } else {
            fprintf(stderr, "Invalid code format: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}