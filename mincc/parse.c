#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

char *mystrndup(const char *s, size_t n) {
    char* new = malloc(n+1);
    if (new) {
        strncpy(new, s, n);
        new[n] = '\0';
    } else {
        error("Memory allocation failed");
    }
    return new;
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

char *expect_ident() {
    if (token->type != TOKEN_IDENT) {
        error_at(token->loc, "Expected an identifier, but got '%s'", token->str);
    }
    char *name = token->str;
    token = token->next;
    return name;
}

bool is_number_node() {
    return token->type == TOKEN_NUMBER;
}

// Check if the current token is EOF
bool at_eof() {
    return token->type == TOKEN_EOF;
}

Token *new_token(TokenType type, Token *current, const char *str, unsigned long size, long val, char *loc) {
    Token *tok = calloc(1, sizeof(Token));
    tok->type = type;
    if (str) {
        tok->str = mystrndup(str, size);
    }
    tok->size = size;
    tok->value = val;
    tok->loc = loc;
    current->next = tok;
    return tok;
}

void print_token_list(Token *head) {
    Token *cur = head;
    while (cur) {
        switch (cur->type) {
            case TOKEN_EOF:
                fprintf(stderr, "TOKEN_EOF\n");
                break;
            case TOKEN_NUMBER:
                fprintf(stderr, "TOKEN_NUMBER: %ld\n", cur->value);
                break;
            case TOKEN_RESERVED:
                fprintf(stderr, "TOKEN_RESERVED: %s\n", cur->str);
                break;
            case TOKEN_IDENT:
                fprintf(stderr, "TOKEN_IDENT: %s\n", cur->str);
                break;
            default:
                fprintf(stderr, "Unknown token type\n");
                break;
        }
        cur = cur->next;
    }
}

int isalphanumub(char c) {
    return  ('a' <= c && c <= 'z') ||
            ('A' <= c && c <= 'Z') ||
            ('0' <= c && c <= '9') ||
            (c == '_');
}

int isalphaub(char c) {
    return  ('a' <= c && c <= 'z') ||
            ('A' <= c && c <= 'Z') ||
            (c == '_');
}

/*****************************************************************
ident_name = [a-zA-Z_][a-zA-Z0-9_]*
******************************************************************/
unsigned long read_ident_size(const char *p) {
    const char *start = p;
    if (!isalphaub(*p)) {
        return 0;
    }
    p++;
    while (isalphanumub(*p)) {
        p++;
    }
    return p - start;
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
            cur = new_token(TOKEN_RESERVED, cur, p, 2, 0, (char *)p);
            p += 2;
            continue;
        }

        if (*p == '+' || *p == '-' || *p == '*' || *p == '(' || *p == ')' || *p == '<' || *p == '>' || *p == '=' || *p == ';') {
            cur = new_token(TOKEN_RESERVED, cur, p, 1, 0, (char *)p);
            p++;
            continue;
        }

        unsigned long ident_size = read_ident_size(p);
        if (ident_size > 0) {
            cur = new_token(TOKEN_IDENT, cur, p, ident_size, 0, (char *)p);
            p += ident_size;
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
    print_token_list(head.next);
    return head.next;
}
