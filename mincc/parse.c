#include "parse.h"

Token *token;

// Consume a token if it matches the expected string
// Return true if matched, false otherwise
// If reached EOF, return false
bool consume_la(const char *op, char *loc) {
    if (token->type == TOKEN_EOF) {
        return false;
    }
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        return false;
    }
    loc = token->loc;
    token = token->next;
    return true;
}

// Consume a token if it matches the expected string
// Return true if matched, false otherwise
// If reached EOF, throw an error
bool consume(const char *op, char *loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected '%s', but got EOF", op);
    }
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        return false;
    }
    loc = token->loc;
    token = token->next;
    return true;
}

// Consume a token if it matches the expected string
// Otherwise, throw an error
void expect(const char *op, char *loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected '%s', but got EOF", op);
    }
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        error_at(token->loc, "Expected '%s', but got '%s'", op, token->str);
    }
    loc = token->loc;
    token = token->next;
}

// Expect a number token and return its value
// Otherwise, throw an error
long expect_number(char *loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected a number, but got EOF");
    }
    if (token->type != TOKEN_NUMBER) {
        error_at(token->loc, "Expected a number, but got '%s'", token->str);
    }
    loc = token->loc;
    long val = token->value;
    token = token->next;
    return val;
}

// Expect an identifier token and return its string
// Otherwise, throw NULL
char *expect_ident(char *loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected an identifier, but got EOF");
    }
    if (token->type != TOKEN_IDENT) {
        error_at(token->loc, "Expected an identifier, but got '%s'", token->str);
    }
    char *name = token->str;
    loc = token->loc;
    token = token->next;
    return name;
}

bool is_number_token() {
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

bool isalphanumub(char c) {
    return  isalnum(c) ||
            (c == '_');
}

bool isalphaub(char c) {
    return  isalpha(c) ||
            (c == '_');
}

Token *cur;

size_t tokenize_reserved(const char *p, const char *keyword, size_t kw_len) {
    if (strncmp(p, keyword, kw_len) == 0 && !isalphanumub(p[kw_len])) {
        cur = new_token(TOKEN_RESERVED, cur, p, kw_len, 0, (char *)p);
        return kw_len;
    }
    return 0;
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
    cur = &head;

    while (*p) {
        // Skip whitespace
        if (isspace(*p)) {
            p++;
            continue;
        }
        size_t len;
        len = tokenize_reserved(p, "return", 6);
        if (len) {
            p += len;
            continue;
        }
        len = tokenize_reserved(p, "if", 2);
        if (len) {
            p += len;
            continue;
        }

        len = tokenize_reserved(p, "else", 4);
        if (len) {
            p += len;
            continue;
        }

        len = tokenize_reserved(p, "for", 3);
        if (len) {
            p += len;
            continue;
        }

        len = tokenize_reserved(p, "while", 5);
        if (len) {
            p += len;
            continue;
        }


        if (strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0 || strncmp(p, "<=", 2) == 0 || strncmp(p, ">=", 2) == 0) {
            cur = new_token(TOKEN_RESERVED, cur, p, 2, 0, (char *)p);
            p += 2;
            continue;
        }

        if (*p == '+' || *p == '-' || *p == '*' || *p == '(' || *p == ')' || *p == '<' || *p == '>' || *p == '=' || *p == ';' || *p == '{' || *p == '}' || *p == ',' ) {
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
                warn_at((char *)p, "Number out of range");
            }
            cur = new_token(TOKEN_NUMBER, cur, NULL, 0, val & 0xff, (char *)p);
            p = q;
            continue;
        }

        error_at((char *)p, "Invalid token");
    }

    new_token(TOKEN_EOF, cur, NULL, 0, 0, (char *)p);
    print_token_list(head.next);
    return head.next;
}
