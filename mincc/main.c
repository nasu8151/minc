#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
#include "ast.h"
#include "codegen.h"
#include "errorhandle.h"

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
    while (fgets(line, sizeof(line) - 1, stdin)) {
        line[sizeof(line) - 1] = '\0'; // null terminate
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

    // crt0
    printf("call __on_entry\n");
    printf("call main\n");
    printf("push r0\n");
    printf("halt\n");
    program();

    return EXIT_SUCCESS;
}