#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

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

// simple strdup fallback for strict C environments
static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) {
        fprintf(stderr, "Error: out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(p, s, n);
    return p;
}

typedef struct {
    char *name;
    int address; // instruction index
} Symbol;

typedef struct {
    int index;    // opcode index to patch
    char *name;   // label to resolve
} Fixup;

typedef struct {
    uint16_t *data;
    size_t size;
    size_t cap;
} CodeVec;

static void codevec_init(CodeVec *v) {
    v->data = NULL; v->size = 0; v->cap = 0;
}
static int codevec_push(CodeVec *v, uint16_t word) {
    if (v->size == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 64;
        uint16_t *nd = (uint16_t *)realloc(v->data, ncap * sizeof(uint16_t));
        if (!nd) return 0;
        v->data = nd; v->cap = ncap;
    }
    v->data[v->size++] = word;
    return 1;
}

typedef struct {
    Symbol *syms; size_t syms_size; size_t syms_cap;
    Fixup  *fix;  size_t fix_size;  size_t fix_cap;
} LinkState;

static void ls_init(LinkState *ls) {
    ls->syms = NULL; ls->syms_size = ls->syms_cap = 0;
    ls->fix  = NULL; ls->fix_size  = ls->fix_cap  = 0;
}

static int add_symbol(LinkState *ls, const char *name, int addr) {
    // check duplicate
    for (size_t i = 0; i < ls->syms_size; i++) {
        if (strcmp(ls->syms[i].name, name) == 0) {
            fprintf(stderr, "Error: Duplicate label '%s'\n", name);
            return 0;
        }
    }
    if (ls->syms_size == ls->syms_cap) {
        size_t ncap = ls->syms_cap ? ls->syms_cap * 2 : 64;
        Symbol *ns = (Symbol *)realloc(ls->syms, ncap * sizeof(Symbol));
        if (!ns) return 0;
        ls->syms = ns; ls->syms_cap = ncap;
    }
    ls->syms[ls->syms_size].name = dupstr(name);
    ls->syms[ls->syms_size].address = addr;
    ls->syms_size++;
    return 1;
}

static int find_symbol(LinkState *ls, const char *name) {
    for (size_t i = 0; i < ls->syms_size; i++) {
        if (strcmp(ls->syms[i].name, name) == 0) return ls->syms[i].address;
    }
    return -1;
}

static int add_fixup(LinkState *ls, int index, const char *name) {
    if (ls->fix_size == ls->fix_cap) {
        size_t ncap = ls->fix_cap ? ls->fix_cap * 2 : 64;
        Fixup *nf = (Fixup *)realloc(ls->fix, ncap * sizeof(Fixup));
        if (!nf) return 0;
        ls->fix = nf; ls->fix_cap = ncap;
    }
    ls->fix[ls->fix_size].index = index;
    ls->fix[ls->fix_size].name = dupstr(name);
    ls->fix_size++;
    return 1;
}

static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

static char *lskip(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int is_valid_label_name(const char *s) {
    if (!(*s == '_' || isalpha((unsigned char)*s))) return 0;
    s++;
    while (*s) {
        if (!(*s == '_' || isalnum((unsigned char)*s))) return 0;
        s++;
    }
    return 1;
}

// Process optional leading label: updates *pp to start of instruction part.
// Returns 1 if the line is only a label (should skip instruction parsing),
// 0 if instruction follows (continue parsing), and -1 on error.
static int process_label(char **pp, LinkState *ls, int instr_index) {
    char *p = *pp;
    char *colon = strchr(p, ':');
    if (colon) {
        char *ws = p;
        while (*ws && *ws != ' ' && *ws != '\t') ws++;
        if (colon < ws) {
            char labbuf[128];
            size_t len = (size_t)(colon - p);
            if (len >= sizeof(labbuf)) {
                fprintf(stderr, "Error: Label too long\n");
                return -1;
            }
            memcpy(labbuf, p, len);
            labbuf[len] = '\0';
            if (!is_valid_label_name(labbuf)) {
                fprintf(stderr, "Error: Invalid label name '%s'\n", labbuf);
                return -1;
            }
            if (!add_symbol(ls, labbuf, instr_index)) {
                return -1;
            }
            p = lskip(colon + 1);
            *pp = p;
            if (*p == '\0') return 1; // label-only line
        }
    }
    return 0;
}

int main(){
    const char *insts_2ops[] = { "mov", "add", "sub", "lt", "mul" };
    const char *insts_1op_rs[]  = { "push", "lds" };
    const char *insts_1op_rd[]  = { "pop", "sts" };
    const char *insts_op_imm[] = { "mvi",  "ldm" };
    const char *insts_imm_op[] = { "stm" };
    const char *insts_addr[] = { "call" };
    const char *insts_op_addr[] = { "jz", "jnz" };
    const char *insts_noopr[] = { "ret" };
    const InstType inst_dict[] = {
        {"mov",  0x0000}, {"add", 0x0100}, {"sub", 0x0200}, {"lt", 0x0300}, {"mul", 0x0400},
        {"push", 0x0800}, {"lds", 0x0900}, {"pop", 0x0A00}, {"sts", 0x0B00}, 
        {"ret",  0x0C00}, 

        {"mvi",  0x1000}, {"stm", 0x2000}, {"ldm", 0x3000}, {"jz", 0x4000}, {"call", 0x5000}, {"jnz", 0x6000},
    };
    char line_to_assemble[256];

    CodeVec code;
    LinkState ls;
    codevec_init(&code);
    ls_init(&ls);
    int instr_index = 0; // counts only real instructions

    while (fgets(line_to_assemble, sizeof(line_to_assemble), stdin)) {
        char *cr_lf = strpbrk(line_to_assemble, "\r\n"); //改行コードを排除
        if (cr_lf) {
            *cr_lf = '\0';
        }
        // Skip leading whitespace and ignore empty/comment lines
        char *p = lskip(line_to_assemble);
        if (*p == '\0') continue;
        if (*p == ';' || *p == '#') continue;

        // handle label via helper
        {
            int lr = process_label(&p, &ls, instr_index);
            if (lr < 0) return EXIT_FAILURE;
            if (lr > 0) continue; // label-only line
        }

        // parse instruction from p
        rtrim(p);
        char *first_space = strchr(p, ' ');
        char instruction[32] = {'\0'};
        if (first_space == NULL) {
            strncpy(instruction, p, sizeof(instruction) - 1);
            instruction[sizeof(instruction) - 1] = '\0';
        } else {
            size_t n = (size_t)(first_space - p);
            if (n >= sizeof(instruction)) n = sizeof(instruction) - 1;
            memcpy(instruction, p, n);
            instruction[n] = '\0';
        }

        int opcode = find_opcode(instruction, inst_dict, sizeof(inst_dict)/sizeof(inst_dict[0]));
        if (opcode == -1) {
            fprintf(stderr, "Error: Unknown instruction '%s'\n", instruction);
            return EXIT_FAILURE;
        }

        if (is_in_array(instruction, insts_2ops, sizeof(insts_2ops)/sizeof(insts_2ops[0]))) {
            // Handle 2-operand instructions
            char *comma = find_comma(first_space + 1, instruction);
            if (comma == NULL) return EXIT_FAILURE;
            char* operand1 = first_space + 1;
            char* operand2 = comma + 1;
            int reg1 = strtol(operand1 + 1, NULL, 10);
            int reg2 = strtol(operand2 + 1, NULL, 10);
            if (!check_register_range(reg1) || !check_register_range(reg2)) return EXIT_FAILURE;
            opcode = opcode | (reg1 << 4) | reg2;
        } else if (is_in_array(instruction, insts_1op_rd, sizeof(insts_1op_rd)/sizeof(insts_1op_rd[0]))){
            // Handle 1-operand instructions
            char* operand = first_space + 1;
            int reg = strtol(operand + 1, NULL, 10);
            opcode = opcode | (reg << 4);
        } else if (is_in_array(instruction, insts_1op_rs, sizeof(insts_1op_rs)/sizeof(insts_1op_rs[0]))){
            // Handle 1-operand instructions
            char* operand = first_space + 1;
            int reg = strtol(operand + 1, NULL, 10);
            opcode = opcode | reg;
        } else if (is_in_array(instruction, insts_op_imm, sizeof(insts_op_imm)/sizeof(insts_op_imm[0]))){
            // Handle op-imm instructions
            char *comma = find_comma(first_space + 1, instruction);
            if (comma == NULL) return EXIT_FAILURE;
            char* operand = first_space + 1;
            char* immediate = comma + 1;
            int reg = strtol(operand + 1, NULL, 10);
            int imm = strtol(immediate, NULL, 0);
            if (!check_immediate_range(imm) || !check_register_range(reg)) return EXIT_FAILURE;
            opcode = opcode | (imm << 4) | reg;
        } else if (is_in_array(instruction, insts_imm_op, sizeof(insts_imm_op)/sizeof(insts_imm_op[0]))){
            // Handle imm-op instructions
            char *comma = find_comma(first_space + 1, instruction);
            if (comma == NULL) return EXIT_FAILURE;
            char* immediate = first_space + 1;
            char* operand = comma + 1;
            int imm = strtol(immediate, NULL, 0);
            int reg = strtol(operand + 1, NULL, 10);
            if (!check_immediate_range(imm) || !check_register_range(reg)) return EXIT_FAILURE;
            opcode = opcode | (imm << 4) | reg;
        } else if (is_in_array(instruction, insts_noopr, sizeof(insts_noopr)/sizeof(insts_noopr[0]))){
            // nothing
        } else if (is_in_array(instruction, insts_addr, sizeof(insts_addr)/sizeof(insts_addr[0]))){
            // If operand is label, create fixup; otherwise immediate numeric
            char* address_str = first_space + 1;
            address_str = lskip(address_str);
            if (*address_str == '\0') {
                fprintf(stderr, "Error: Missing address operand for '%s'\n", instruction);
                return EXIT_FAILURE;
            }
            // decide label or number
            if (*address_str == '_' || isalpha((unsigned char)*address_str)) {
                // token until whitespace or end
                char tok[128];
                size_t ti = 0;
                while (address_str[ti] && address_str[ti] != ' ' && address_str[ti] != '\t') {
                    if (ti + 1 >= sizeof(tok)) {
                        fprintf(stderr, "Error: Label too long\n");
                        return EXIT_FAILURE;
                    }
                    tok[ti] = address_str[ti];
                    ti++;
                }
                tok[ti] = '\0';
                if (!is_valid_label_name(tok)) {
                    fprintf(stderr, "Error: Invalid label name '%s'\n", tok);
                    return EXIT_FAILURE;
                }
                // emit placeholder; patch later
                if (!codevec_push(&code, (uint16_t)opcode)) {
                    fprintf(stderr, "Error: out of memory\n");
                    return EXIT_FAILURE;
                }
                if (!add_fixup(&ls, (int)(code.size - 1), tok)) return EXIT_FAILURE;
                instr_index++;
                continue; // next line
            } else {
                int address = strtol(address_str, NULL, 0);
                if (!check_immediate_range(address)) return EXIT_FAILURE;
                opcode = opcode | (address << 4);
            }
        } else if (is_in_array(instruction, insts_op_addr, sizeof(insts_op_addr)/sizeof(insts_op_addr[0]))){
            // Handle addr-op (jz, jnz): first is address (label or number), second is rs
            char *comma = find_comma(first_space + 1, instruction);
            if (comma == NULL) return EXIT_FAILURE;
            char* immediate = first_space + 1;
            char* operand = comma + 1;
            int reg = strtol(operand + 1, NULL, 10);
            if (!check_register_range(reg)) return EXIT_FAILURE;
            char *address_str = lskip(immediate);
            if (*address_str == '_' || isalpha((unsigned char)*address_str)) {
                // parse label token
                char tok[128];
                size_t ti = 0;
                while (address_str[ti] && address_str[ti] != ' ' && address_str[ti] != '\t' && address_str[ti] != ',') {
                    if (ti + 1 >= sizeof(tok)) {
                        fprintf(stderr, "Error: Label too long\n");
                        return EXIT_FAILURE;
                    }
                    tok[ti] = address_str[ti];
                    ti++;
                }
                tok[ti] = '\0';
                if (!is_valid_label_name(tok)) {
                    fprintf(stderr, "Error: Invalid label name '%s'\n", tok);
                    return EXIT_FAILURE;
                }
                // emit opcode with rs set; address will be backpatched later
                uint16_t word = (uint16_t)(opcode | (reg & 0xF));
                if (!codevec_push(&code, word)) {
                    fprintf(stderr, "Error: out of memory\n");
                    return EXIT_FAILURE;
                }
                if (!add_fixup(&ls, (int)(code.size - 1), tok)) return EXIT_FAILURE;
                instr_index++;
                continue;
            } else {
                int addr = strtol(address_str, NULL, 0);
                if (!check_immediate_range(addr)) return EXIT_FAILURE;
                opcode = opcode | ((addr & 0xFF) << 4) | (reg & 0xF);
            }
        } else {
            fprintf(stderr, "Error: Unhandled instruction type for '%s'\n", instruction);
            return EXIT_FAILURE;
        }

        if (!codevec_push(&code, (uint16_t)opcode)) {
            fprintf(stderr, "Error: out of memory\n");
            return EXIT_FAILURE;
        }
        instr_index++;
    }

    // resolve fixups
    for (size_t i = 0; i < ls.fix_size; i++) {
        Fixup *f = &ls.fix[i];
        int addr = find_symbol(&ls, f->name);
        if (addr < 0) {
            fprintf(stderr, "Error: Undefined label '%s'\n", f->name);
            return EXIT_FAILURE;
        }
        if (!check_immediate_range(addr)) {
            return EXIT_FAILURE;
        }
        // keep top nibble and low nibble, set middle 8 bits with (addr<<4)
        code.data[f->index] = (uint16_t)((code.data[f->index] & 0xF00F) | ((addr & 0xFF) << 4));
    }

    // output
    for (size_t i = 0; i < code.size; i++) {
        printf("%04X\n", code.data[i]);
    }

    return EXIT_SUCCESS;
}