#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]){
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <infile> <outfile>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *input = fopen(argv[1], "rb");
    if (!input) {
        perror("Error opening input file");
        return EXIT_FAILURE;
    }
    FILE *output = fopen(argv[2], "wb");
    if (!output) {
        perror("Error opening output file");
        return EXIT_FAILURE;
    }

    char line_to_assemble[256];

    while (fgets(line_to_assemble, sizeof(line_to_assemble), input)) {
        printf("Assembling: %s\n", line_to_assemble);
        char *first_space = strchr(line_to_assemble, ' ');
        if (first_space) {
            char instruction[32];
            strncpy(instruction, line_to_assemble, first_space - line_to_assemble);
            instruction[first_space - line_to_assemble] = '\0';  // Null-terminate the string

            const char *operand = first_space + 1;
            int opr = strtol(operand, NULL, 0);
            printf("Parsed instruction: %s, operand: %d\n", instruction, opr);

            if (opr < 0 || opr > 0xFF) {
                fprintf(stderr, "Operand out of range: %s\n", operand);
                fclose(output);
                fclose(input);
                return EXIT_FAILURE;
            }

            if (strcmp(instruction, "ld") == 0) {
                fprintf(output, "%03x\n", 0x000 | opr);
            } else if(strcmp(instruction, "add") == 0) {
                fprintf(output, "%03x\n", 0x100 | opr);
            } else if(strcmp(instruction, "sub") == 0) {
                fprintf(output, "%03x\n", 0x200 | opr);
            } else {
                fprintf(stderr, "Unsupported instruction: %s\n", instruction);
                fclose(output);
                fclose(input);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Invalid assembly format\n");
            fclose(output);
            fclose(input);
            return EXIT_FAILURE;
        }
    }

    fclose(output);
    fclose(input);
    return EXIT_SUCCESS;
}