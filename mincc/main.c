#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mincc.h"

Token *token;

static char *user_input;

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
}

void warn_at(char *loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int pos = loc - user_input;
    fprintf(stderr, "           %s\n", user_input);
    fprintf(stderr, "[Warning]: ");
    fprintf(stderr, "%*s", pos, ""); // pos個の空白を出力
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}



int main() {
    // if (argc != 2) {
    //     fprintf(stderr, "Usage : <code>\n");
    //     return EXIT_FAILURE;
    // }
    char line[256];
    char *code = calloc(1, 1);
    if (!code) {
        error("out of memory");
    }
    while (fgets(line, sizeof(line), stdin)) {
        char *cr_lf = strpbrk(line, "\r\n"); //改行コードを排除
        if (cr_lf) {
            *cr_lf = '\0';
        }
        char *new_code = (char *)realloc(code, strlen(code) + strlen(line) + 1);
        if (!new_code) {
            error("out of memory");
        }
        code = new_code;
        strcat(code, line);
    }
    fprintf(stderr, "Input code: %s\n", code);
    user_input = code;

    token = tokenize(user_input);

    printf("call main\n");
    printf("push r0\n");
    printf("halt\n");
    printf("main: ");
    program();

    return EXIT_SUCCESS;
}