#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <code>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *code = argv[1];
    printf("ld %s\n", code);
    return EXIT_SUCCESS;
}