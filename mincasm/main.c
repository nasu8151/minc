#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]){
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <assembly> <outfile>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *output = fopen(argv[2], "wb");
    if (!output) {
        perror("Error opening output file");
        return EXIT_FAILURE;
    }

    const char *assembly = argv[1];

    char *first_space = strchr(assembly, ',');
    if (first_space) {
        char instruction[32];
        strncpy(instruction, assembly, first_space - assembly);
        if (strcmp(instruction, "ld") == 0) {
            const char *operand = first_space + 1;
            int opr = strtol(operand, NULL, 0);
            fprintf(output, "%02x\n", opr & 0xFF);
        } else {
            fprintf(stderr, "Unsupported instruction: %s\n", instruction);
            fclose(output);
            return EXIT_FAILURE;
        }
    } else {
        fprintf(stderr, "Invalid assembly format\n");
        fclose(output);
        return EXIT_FAILURE;
    }

    fclose(output);
    return EXIT_SUCCESS;
}