#include <stdio.h>
#include <stdlib.h>

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