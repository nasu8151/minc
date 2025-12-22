#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

Token *token;

static char user_input[1024];

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

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[Warning]: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}

void warn_at(char *loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int pos = loc - user_input;
    fprintf(stderr, "         %s\n", user_input);
    fprintf(stderr, "[Warning]: ");
    fprintf(stderr, "%*s", pos, ""); // pos個の空白を出力
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(EXIT_FAILURE);
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