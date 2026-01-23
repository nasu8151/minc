#include "errorhandle.h"

char *user_input;

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
    char *line_start = loc;
    while (user_input < line_start && *(line_start - 1) != '\n') {
        line_start--;
    }
    char *line_end = loc;
    while (*line_end != '\n' && *line_end != '\0') {
        line_end++;
    }
    unsigned long line_num = 1;
    for (char *p = user_input; p < line_start; p++) {
        if (*p == '\n') {
            line_num++;
        }
    }
    char *line_buf = mystrndup(line_start, line_end - line_start);
    int pos = loc - line_start;
    fprintf(stderr, "At line %lu:\n", line_num);
    fprintf(stderr, "         %s\n", line_buf);
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
    char *line_start = loc;
    while (user_input < line_start && *(line_start - 1) != '\n') {
        line_start--;
    }
    char *line_end = loc;
    while (*line_end != '\n' && *line_end != '\0') {
        line_end++;
    }
    unsigned long line_num = 1;
    for (char *p = user_input; p < line_start; p++) {
        if (*p == '\n') {
            line_num++;
        }
    }
    char *line_buf = mystrndup(line_start, line_end - line_start);
    int pos = loc - line_start;
    fprintf(stderr, "At line %lu:\n", line_num);
    fprintf(stderr, "          %s\n", line_buf);
    fprintf(stderr, "[Warning]: ");
    fprintf(stderr, "%*s", pos, ""); // pos個の空白を出力
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    free(line_buf);
}

