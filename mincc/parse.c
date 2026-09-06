#include "parse.h"

Token *token;

// Consume a token if it matches the expected string
// Return true if matched, false otherwise
// If reached EOF, return false
bool consume_la(const char *op, char **loc) {
    if (token->type == TOKEN_EOF) {
        return false;
    }
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        return false;
    }
    *loc = token->loc;
    token = token->next;
    return true;
}

// Consume a token if it matches the expected string
// Return true if matched, false otherwise
// If reached EOF, throw an error
bool consume(const char *op, char **loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected '%s', but got EOF", op);
    }
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        return false;
    }
    *loc = token->loc;
    token = token->next;
    return true;
}

// Consume tokens if they match the valid type.
// Returns Type_t* if mached, otherwise returns NULL.
// If reached EOF, return NULL.
Type_t *check_type(char **loc) {
    Type_t *type = calloc(1, sizeof(Type_t));
    type->size = -1;
    type->type = TY_INT;
    if (consume_la("uint8_t", loc) || consume_la("char", loc)) {
        type->size = 1; // Currently uint8_t, int and char mean the same (1 byte int) type.
    } else if (consume_la("int", loc)) {
        type->size = 2;
    } else if (consume_la("void", loc)) {
        type->size = 0; // void type has size 0
    } else {
        free(type);
        return NULL;
    }
    Type_t *cur = type;
    while (consume_la("*", loc)) {
        Type_t *new_ptr = calloc(1, sizeof(Type_t));
        if (!new_ptr) {
            error("Memory allocation failed");
        }
        new_ptr->type = TY_PTR;
        new_ptr->size = PTR_SIZE;
        new_ptr->ptr_to = cur;
        cur = new_ptr;
    }
    type = cur;
    return type;
}

// Consume a token if it matches the expected string
// Otherwise, throw an error
void expect(const char *op, char **loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected '%s', but got EOF", op);
    }
    if (token->type != TOKEN_RESERVED || strcmp(token->str, op) != 0) {
        error_at(token->loc, "Expected '%s', but got '%s'", op, token->str);
    }
    *loc = token->loc;
    token = token->next;
}

// Expect a number token and return its value
// Otherwise, throw an error
long expect_number(char **loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected a number, but got EOF");
    }
    if (token->type != TOKEN_NUMBER) {
        error_at(token->loc, "Expected a number, but got '%s'", token->str);
    }
    *loc = token->loc;
    long val = token->value;
    token = token->next;
    return val;
}

// Expect an identifier token and return its string
// Otherwise, throw NULL
char *expect_ident(char **loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected an identifier, but got EOF");
    }
    if (token->type != TOKEN_IDENT) {
        error_at(token->loc, "Expected an identifier, but got '%s'", token->str);
    }
    char *name = token->str;
    *loc = token->loc;
    token = token->next;
    return name;
}

// Expect a string literal token and return its (escape-decoded) contents
// Otherwise, throw an error
char *expect_string(char **loc) {
    if (token->type == TOKEN_EOF) {
        error_at(token->loc, "Expected a string literal, but got EOF");
    }
    if (token->type != TOKEN_STRING) {
        error_at(token->loc, "Expected a string literal, but got '%s'", token->str);
    }
    char *str = token->str;
    *loc = token->loc;
    token = token->next;
    return str;
}

bool is_number_token() {
    return token->type == TOKEN_NUMBER;
}

bool is_string_token() {
    return token->type == TOKEN_STRING;
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
    tok->len = size;
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
            case TOKEN_STRING:
                fprintf(stderr, "TOKEN_STRING: \"%s\"\n", cur->str);
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

const char* reserved_words[] = {
    "return",
    "if",
    "else",
    "for",
    "while",
    "int",
    "uint8_t",
    "char",
    "void",
    "break",
    "asm",
    NULL
};

/*****************************************************************
string_literal = '"' (escape | [^"\\\n])* '"'
escape         = "\\" ["ntr\\\"']

Reads one string literal starting at *pp (which must point at the opening
quote), returns the escape-decoded contents in a fresh buffer, and advances
*pp past the closing quote. `\0` is deliberately unsupported: the result is
handled as a NUL-terminated C string throughout, so an embedded NUL would
silently truncate it.
******************************************************************/
char *read_string_literal(const char **pp) {
    const char *open = *pp;
    const char *p = open + 1; // skip the opening quote
    size_t cap = 16, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        error("Memory allocation failed");
    }
    while (*p != '"') {
        char c;
        if (*p == '\0' || *p == '\n') {
            error_at((char *)open, "Unterminated string literal");
        }
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case '\\': c = '\\'; break;
            case '"':  c = '"';  break;
            case '\'': c = '\''; break;
            default:
                error_at((char *)p - 1, "Unknown escape sequence '\\%c'", *p);
            }
            p++;
        } else {
            c = *p++;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) {
                error("Memory allocation failed");
            }
            buf = newbuf;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    *pp = p + 1; // skip the closing quote
    return buf;
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
        bool matched = false;
        char **rp = (char **)reserved_words;
        while (*rp) {
            len = tokenize_reserved(p, *rp, strlen(*rp));
            if (len) {
                p += len;
                matched = true;
                break;
            }
            rp++;
        }
        if (matched) {
            continue;
        }

        if (*p == '"') {
            char *loc = (char *)p;
            char *str = read_string_literal(&p);
            cur = new_token(TOKEN_STRING, cur, str, strlen(str), 0, loc);
            free(str); // new_token keeps its own copy
            continue;
        }

        if (strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0 || strncmp(p, "<=", 2) == 0 || strncmp(p, ">=", 2) == 0 ||
            strncmp(p, "||", 2) == 0 || strncmp(p, "&&", 2) == 0) {
            cur = new_token(TOKEN_RESERVED, cur, p, 2, 0, (char *)p);
            p += 2;
            continue;
        }

        if (*p == '+' || *p == '-' || *p == '*' || *p == '(' || *p == ')' || *p == '<' || *p == '>' || *p == '=' || *p == ';' ||
            *p == '{' || *p == '}' || *p == ',' || *p == '[' || *p == ']' || *p == '|' || *p == '&' || *p == '^' || *p == '~' ||
            *p == '!') {
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
            long val;
            if (strncmp(p, "0b", 2) == 0) {
                val = strtol(p + 2, &q, 2);
            } else if(strncmp(p, "0x", 2) == 0){
                val = strtol(p + 2, &q, 16);
            } else {
                val = strtol(p, &q, 0);
            }
            if (val < -0x10000 || val > 0xFFFF) {
                warn_at((char *)p, "Number out of range");
            }
            cur = new_token(TOKEN_NUMBER, cur, NULL, 0, val & 0xFFFF, (char *)p);
            p = q;
            continue;
        }

        error_at((char *)p, "Invalid token");
    }

    new_token(TOKEN_EOF, cur, NULL, 0, 0, (char *)p);
    print_token_list(head.next);
    return head.next;
}
