#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct insttype {
    const char *name;
    int opcode;
} InstType;

int find_opcode(const char *inst_name, const InstType *inst_dict, size_t dict_size) {
    for (size_t i = 0; i < dict_size; i++) {
        if (strcmp(inst_name, inst_dict[i].name) == 0) {
            return inst_dict[i].opcode;
        }
    }
    return -1; // Not found
}

int is_in_array(const char *inst, const char **inst_array, size_t array_size) {
    for (size_t i = 0; i < array_size; i++) {
        if (strcmp(inst, inst_array[i]) == 0) {
            return 1; // Found
        }
    }
    return 0; // Not found
}

int check_register_range(int reg) {
    if (reg < 0 || reg > 15) {
        fprintf(stderr, "Error: Register out of range: r%d\n", reg);
        return 0; // Out of range
    }
    return 1; // In range
}

int check_immediate_range(int imm) {
    if (imm < 0 || imm > 255) {
        fprintf(stderr, "Error: Immediate value out of range: %d\n", imm);
        return 0; // Out of range
    }
    return 1; // In range
}

char *find_comma(char *str, char *inst) {
    char *comma = strchr(str, ',');
    if (comma == NULL) {
        fprintf(stderr, "Error: Expected two arguments in instruction '%s'\n", inst);
        return NULL;
    }
    return comma;
}

int main(){
    const char *insts_2ops[] = { "mov", "add", "sub", "mul" };
    const char *insts_1op_rs[]  = { "push", "lds" };
    const char *insts_1op_rd[]  = { "pop", "sts" };
    const char *insts_op_imm[] = { "mvi",  "ldm" };
    const char *insts_imm_op[] = { "stm", "jz" };
    const char *insts_op_addr[] = { "call" };
    const char *insts_noopr[] = { "ret" };
    const InstType inst_dict[] = {
        {"mov",  0x0000},                  {"add", 0x0100},                                   {"sub", 0x0200}, {"mul", 0x0300},
        {"push", 0x0400}, {"lds", 0x0410}, {"pop", 0x0500}, {"sts", 0x0501}, {"ret", 0x0502}, 

        {"mvi",  0x1000}, {"stm", 0x2000}, {"ldm", 0x3000}, {"jz", 0x4000}, {"call", 0x5000},
    };
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

        int opcode = find_opcode(instruction, inst_dict, sizeof(inst_dict)/sizeof(inst_dict[0]));

        if (opcode == -1) {
            fprintf(stderr, "Error: Unknown instruction '%s'\n", instruction);
            return EXIT_FAILURE;
        }

        if (is_in_array(instruction, insts_2ops, sizeof(insts_2ops)/sizeof(insts_2ops[0]))) {
            // Handle 2-operand instructions
            char *comma = find_comma(first_space + 1, instruction);
            if (comma == NULL) {
                return EXIT_FAILURE;
            }
            char* operand1 = first_space + 1;
            char* operand2 = comma + 1;
            int reg1 = strtol(operand1 + 1, NULL, 10); // Skip 'r'
            int reg2 = strtol(operand2 + 1, NULL, 10);
            if (!check_register_range(reg1) || !check_register_range(reg2)) {
                return EXIT_FAILURE;
            }
            // printf("%02d, %02d\n", reg1, reg2);
            opcode = opcode | (reg1 << 4) | reg2;
        } else if (is_in_array(instruction, insts_1op_rd, sizeof(insts_1op_rd)/sizeof(insts_1op_rd[0]))){
            // Handle 1-operand instructions
            char* operand = first_space + 1;
            int reg = strtol(operand + 1, NULL, 10); // Skip 'r'
            opcode = opcode | reg << 4;
        } else if (is_in_array(instruction, insts_1op_rs, sizeof(insts_1op_rs)/sizeof(insts_1op_rs[0]))){
            // Handle 1-operand instructions
            char* operand = first_space + 1;
            int reg = strtol(operand + 1, NULL, 10); // Skip
            opcode = opcode | reg;
        } else if (is_in_array(instruction, insts_op_imm, sizeof(insts_op_imm)/sizeof(insts_op_imm[0]))){
            // Handle op-imm instructions
            char *comma = find_comma(first_space + 1, instruction);
            if (comma == NULL) {
                return EXIT_FAILURE;
            }
            char* operand = first_space + 1;
            char* immediate = comma + 1;
            int reg = strtol(operand + 1, NULL, 10); // Skip 'r'
            int imm = strtol(immediate, NULL, 0);
            if (!check_immediate_range(imm) || !check_register_range(reg)) {
                return EXIT_FAILURE;
            }
            opcode = opcode | imm << 4 | reg;
        } else if (is_in_array(instruction, insts_imm_op, sizeof(insts_imm_op)/sizeof(insts_imm_op[0]))){
            // Handle imm-op instructions
            char *comma = find_comma(first_space + 1, instruction);
            if (comma == NULL) {
                return EXIT_FAILURE;
            }
            char* immediate = first_space + 1;
            char* operand = comma + 1;
            int imm = strtol(immediate, NULL, 0);
            int reg = strtol(operand + 1, NULL, 10); // Skip 'r'
            if (!check_immediate_range(imm) || !check_register_range(reg)) {
                return EXIT_FAILURE;
            }
            opcode = opcode | (imm << 4) | reg;
        } else if (is_in_array(instruction, insts_noopr, sizeof(insts_noopr)/sizeof(insts_noopr[0]))){
            // No additional processing needed for no-oprand instructions
        } else if (is_in_array(instruction, insts_op_addr, sizeof(insts_op_addr)/sizeof(insts_op_addr[0]))){
            // Handle op-addr instructions
            char* address_str = first_space + 1;
            int address = strtol(address_str, NULL, 0);
            if (!check_immediate_range(address)) {
                return EXIT_FAILURE;
            }
            opcode = opcode | address << 4;
        } else {
            fprintf(stderr, "Error: Unhandled instruction type for '%s'\n", instruction);
            return EXIT_FAILURE;
        }
        printf("%04X\n", opcode);
    }

    return EXIT_SUCCESS;
}