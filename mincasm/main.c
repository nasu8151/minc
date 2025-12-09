#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(){

    char line_to_assemble[256];

    while (fgets(line_to_assemble, sizeof(line_to_assemble), stdin)) {
        char *cr_lf = strpbrk(line_to_assemble, "\r\n"); //改行コードを排除
        if (cr_lf) {
            *cr_lf = '\0';
        }
        char *first_space = strchr(line_to_assemble, ' ');
        // printf("Assembling: '%s'\n", line_to_assemble);

        char instruction[32] = {'\0'};
        if (first_space == NULL) {
            strcpy(instruction, line_to_assemble);
            instruction[31] = '\0';                              // Null-terminate the string
        } else {
            strncpy(instruction, line_to_assemble, first_space - line_to_assemble);
            instruction[first_space - line_to_assemble] = '\0';  // Null-terminate the string
        }

        // printf("Parsed instruction: %s\n", instruction);

        if (strcmp(instruction, "push") == 0) {
            if (first_space == NULL) {
                fprintf(stderr, "Invalid assembly format\n");
                return EXIT_FAILURE;
            }
            const char *operand = first_space + 1;
            int opr = strtol(operand, NULL, 0);
            if (opr < 0 || opr > 0xFF) {
                fprintf(stderr, "Operand out of range: %s\n", operand);
                return EXIT_FAILURE;
            }
            // printf("Parsed operand: %d\n", opr);
            printf("%03x\n", 0x000 | opr);
        } else if (first_space == NULL){      // Arithmetic instructions have no oprands.
            if(strcmp(instruction, "add") == 0) {
                printf("%03x\n", 0x400);
            } else if(strcmp(instruction, "sub") == 0) {
                printf("%03x\n", 0x500);
            } else if(strcmp(instruction, "mul") == 0) {
                printf("%03x\n", 0x600);
            } else {
                fprintf(stderr, "Unsupported instruction: '%s'.\n", instruction);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Invalid assembly format\n");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}