#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

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

// Check if the current token is EOF
bool at_eof() {
    return token->type == TOKEN_EOF;
}

Token *new_token(TokenType type, Token *current, const char *str, unsigned long size, long val, char *loc) {
    Token *tok = calloc(1, sizeof(Token));
    if (size > sizeof(tok->str)) {
        size = sizeof(tok->str) - 1;
        warn_at(loc, "Token string is too long, truncated to %ld characters", size);
    }
    tok->type = type;
    if (str) {
        strncpy(tok->str, str, size);
        tok->str[size] = '\0';
    }
    tok->value = val;
    tok->loc = loc;
    current->next = tok;
    return tok;
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

        if (strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0 || strncmp(p, "<=", 2) == 0 || strncmp(p, ">=", 2) == 0) {
            cur = new_token(TOKEN_RESERVED, cur, (char[]){*p, *(p+1), 0}, 2, 0, (char *)p);
            p += 2;
            continue;
        }

        if (*p == '+' || *p == '-' || *p == '*' || *p == '(' || *p == ')' || *p == '<' || *p == '>' ) {
            cur = new_token(TOKEN_RESERVED, cur, (char[]){*p, 0}, 1, 0, (char *)p);
            p++;
            continue;
        }

        if (isdigit(*p)) {
            char *q = (char *)p;
            long val = strtol(p, &q, 0);
            if (val < 0 || val > 0xFF) {
                error_at((char *)p, "Number out of range");
            }
            cur = new_token(TOKEN_NUMBER, cur, NULL, 0, val, (char *)p);
            p = q;
            continue;
        }

        error_at((char *)p, "Invalid token");
    }

    new_token(TOKEN_EOF, cur, NULL, 0, 0, (char *)p);
    return head.next;
}
