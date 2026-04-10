#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512

typedef enum {
    FIX_IMM8,
    FIX_REL12
} FixKind;

typedef struct {
    char *name;
    int address;
} Symbol;

typedef struct {
    int index;
    char *name;
    int line_num;
    FixKind kind;
    uint8_t reg_nibble;
    uint8_t op_nibble;
} Fixup;

typedef struct {
    uint16_t *data;
    size_t size;
    size_t cap;
} CodeVec;

typedef struct {
    Symbol *data;
    size_t size;
    size_t cap;
} SymbolVec;

typedef struct {
    Fixup *data;
    size_t size;
    size_t cap;
} FixupVec;

static int g_line_num;
static char g_line[MAX_LINE];

static void die(const char *msg) {
    fprintf(stderr, "Error: %s\nin line %d\n\"%s\"\n", msg, g_line_num, g_line);
    exit(EXIT_FAILURE);
}

static void die_fmt(const char *prefix, const char *arg) {
    fprintf(stderr, "Error: %s '%s'\nin line %d\n\"%s\"\n", prefix, arg, g_line_num, g_line);
    exit(EXIT_FAILURE);
}

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

static void codevec_push(CodeVec *v, uint16_t x) {
    if (v->size == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 128;
        uint16_t *nd = (uint16_t *)realloc(v->data, ncap * sizeof(uint16_t));
        if (!nd) {
            fprintf(stderr, "Error: out of memory\n");
            exit(EXIT_FAILURE);
        }
        v->data = nd;
        v->cap = ncap;
    }
    v->data[v->size++] = x;
}

static void symbolvec_push(SymbolVec *v, const char *name, int address) {
    for (size_t i = 0; i < v->size; i++) {
        if (strcmp(v->data[i].name, name) == 0) {
            die_fmt("Duplicate label", name);
        }
    }
    if (v->size == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 64;
        Symbol *nd = (Symbol *)realloc(v->data, ncap * sizeof(Symbol));
        if (!nd) {
            fprintf(stderr, "Error: out of memory\n");
            exit(EXIT_FAILURE);
        }
        v->data = nd;
        v->cap = ncap;
    }
    v->data[v->size].name = dupstr(name);
    v->data[v->size].address = address;
    v->size++;
}

static int symbol_find(const SymbolVec *v, const char *name) {
    for (size_t i = 0; i < v->size; i++) {
        if (strcmp(v->data[i].name, name) == 0) {
            return v->data[i].address;
        }
    }
    return -1;
}

static void fixupvec_push(FixupVec *v, int index, const char *name, int line_num, FixKind kind, uint8_t reg_nibble, uint8_t op_nibble) {
    if (v->size == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 64;
        Fixup *nd = (Fixup *)realloc(v->data, ncap * sizeof(Fixup));
        if (!nd) {
            fprintf(stderr, "Error: out of memory\n");
            exit(EXIT_FAILURE);
        }
        v->data = nd;
        v->cap = ncap;
    }
    v->data[v->size].index = index;
    v->data[v->size].name = dupstr(name);
    v->data[v->size].line_num = line_num;
    v->data[v->size].kind = kind;
    v->data[v->size].reg_nibble = reg_nibble;
    v->data[v->size].op_nibble = op_nibble;
    v->size++;
}

static char *lskip(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static void strip_comment(char *s) {
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == ';') {
            s[i] = '\0';
            return;
        }
    }
}

static int is_valid_label_name(const char *s) {
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) {
        return 0;
    }
    s++;
    while (*s) {
        if (!(isalnum((unsigned char)*s) || *s == '_')) {
            return 0;
        }
        s++;
    }
    return 1;
}

static int parse_reg(const char *tok) {
    if (!(tok[0] == 'r' || tok[0] == 'R')) {
        die_fmt("Expected register", tok);
    }
    char *end = NULL;
    long v = strtol(tok + 1, &end, 10);
    if (*end != '\0' || v < 0 || v > 15) {
        die_fmt("Register out of range", tok);
    }
    return (int)v;
}

static int parse_int(const char *tok, int min_v, int max_v) {
    char *end = NULL;
    long v = strtol(tok, &end, 0);
    if (*end != '\0') {
        die_fmt("Expected integer", tok);
    }
    if (v < min_v || v > max_v) {
        die_fmt("Immediate out of range", tok);
    }
    return (int)v;
}

static uint16_t enc_op6_rr(uint8_t op6, uint8_t rd, uint8_t rs) {
    return (uint16_t)(((uint16_t)op6 << 10) | ((uint16_t)rd << 4) | rs);
}

static uint16_t enc_op6_rs(uint8_t op6, uint8_t rs) {
    return (uint16_t)(((uint16_t)op6 << 10) | rs);
}

static uint16_t enc_op6_rd(uint8_t op6, uint8_t rd) {
    return (uint16_t)(((uint16_t)op6 << 10) | ((uint16_t)rd << 4));
}

static uint16_t enc_op4_reg_imm8(uint8_t op4, uint8_t reg, uint8_t imm8) {
    return (uint16_t)(((uint16_t)op4 << 12) | ((uint16_t)(imm8 >> 4) << 8) | ((uint16_t)reg << 4) | (imm8 & 0x0F));
}

static uint16_t enc_op4_rel12(uint8_t op4, uint16_t rel12) {
    uint8_t n_low = (uint8_t)(rel12 & 0x0F);
    uint8_t n_mid = (uint8_t)((rel12 >> 4) & 0x0F);
    uint8_t n_hi = (uint8_t)((rel12 >> 8) & 0x0F);
    return (uint16_t)(((uint16_t)op4 << 12) | ((uint16_t)n_mid << 8) | ((uint16_t)n_hi << 4) | n_low);
}

static int parse_memref(char *tok, int *is_x, int *imm8) {
    if ((tok[0] == 'X' || tok[0] == 'x' || tok[0] == 'Y' || tok[0] == 'y') && tok[1] == '+') {
        *is_x = (tok[0] == 'X' || tok[0] == 'x');
        *imm8 = parse_int(tok + 2, -128, 127);
        return 1;
    }
    *is_x = 0;
    *imm8 = parse_int(tok, -128, 127);
    return 1;
}

static void to_lower_str(char *s) {
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

static char *next_token(char **pctx) {
    char *p = lskip(*pctx);
    if (*p == '\0') {
        *pctx = p;
        return NULL;
    }
    char *start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != ',') {
        p++;
    }
    if (*p == ',') {
        *p = '\0';
        p++;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    } else if (*p) {
        *p = '\0';
        p++;
    }
    *pctx = p;
    return start;
}

int main(void) {
    CodeVec code = {0};
    SymbolVec symbols = {0};
    FixupVec fixups = {0};

    int instr_index = 0;
    g_line_num = 0;

    while (fgets(g_line, sizeof(g_line), stdin)) {
        g_line_num++;
        char *crlf = strpbrk(g_line, "\r\n");
        if (crlf) {
            *crlf = '\0';
        }

        strip_comment(g_line);
        rtrim(g_line);

        char *p = lskip(g_line);
        if (*p == '\0') {
            continue;
        }

        char *colon = strchr(p, ':');
        if (colon) {
            int token_has_space = 0;
            for (char *q = p; q < colon; q++) {
                if (*q == ' ' || *q == '\t') {
                    token_has_space = 1;
                    break;
                }
            }
            if (!token_has_space) {
                *colon = '\0';
                if (!is_valid_label_name(p)) {
                    die_fmt("Invalid label", p);
                }
                symbolvec_push(&symbols, p, instr_index);
                p = lskip(colon + 1);
                if (*p == '\0') {
                    continue;
                }
            }
        }

        char *ctx = p;
        char *inst = next_token(&ctx);
        if (!inst) {
            continue;
        }
        to_lower_str(inst);

        uint16_t word = 0;
        int emit = 1;

        if (!strcmp(inst, "mov") || !strcmp(inst, "or") || !strcmp(inst, "and") || !strcmp(inst, "xor") ||
            !strcmp(inst, "add") || !strcmp(inst, "adc") || !strcmp(inst, "sub") || !strcmp(inst, "sbc") ||
            !strcmp(inst, "lt") || !strcmp(inst, "ltc") || !strcmp(inst, "rr") || !strcmp(inst, "mul") || !strcmp(inst, "mulh")) {
            uint8_t op6;
            if (!strcmp(inst, "mov")) op6 = 0x00;
            else if (!strcmp(inst, "or")) op6 = 0x01;
            else if (!strcmp(inst, "and")) op6 = 0x02;
            else if (!strcmp(inst, "xor")) op6 = 0x03;
            else if (!strcmp(inst, "add")) op6 = 0x04;
            else if (!strcmp(inst, "adc")) op6 = 0x05;
            else if (!strcmp(inst, "sub")) op6 = 0x06;
            else if (!strcmp(inst, "sbc")) op6 = 0x07;
            else if (!strcmp(inst, "lt")) op6 = 0x08;
            else if (!strcmp(inst, "ltc")) op6 = 0x09;
            else if (!strcmp(inst, "rr")) op6 = 0x0A;
            else if (!strcmp(inst, "mul")) op6 = 0x0E;
            else op6 = 0x0F;

            char *r0 = next_token(&ctx);
            if (!r0) die("Missing first operand");
            char *r1 = next_token(&ctx);
            if (!r1) die("Missing second operand");

            int rd = parse_reg(r0);
            int rs = parse_reg(r1);
            word = enc_op6_rr(op6, (uint8_t)rd, (uint8_t)rs);
        } else if (!strcmp(inst, "stf") || !strcmp(inst, "clf")) {
            uint8_t op6 = !strcmp(inst, "stf") ? 0x10 : 0x11;
            char *imm = next_token(&ctx);
            if (!imm) die("Missing immediate for stf/clf");
            if (imm[0] == '#') {
                imm++;
            }
            int c = parse_int(imm, 0, 1);
            word = (uint16_t)(((uint16_t)op6 << 10) | (uint16_t)c);
        } else if (!strcmp(inst, "push") || !strcmp(inst, "sts")) {
            uint8_t op6 = !strcmp(inst, "push") ? 0x1C : 0x1E;
            char *r = next_token(&ctx);
            if (!r) die("Missing operand");
            int rs = parse_reg(r);
            if (!strcmp(inst, "push") && ((rs & 1) != 0)) {
                die_fmt("push requires even register", r);
            }
            word = enc_op6_rs(op6, (uint8_t)rs);
        } else if (!strcmp(inst, "pop") || !strcmp(inst, "lds")) {
            uint8_t op6 = !strcmp(inst, "pop") ? 0x1D : 0x1F;
            char *r = next_token(&ctx);
            if (!r) die("Missing operand");
            int rd = parse_reg(r);
            if (!strcmp(inst, "pop") && ((rd & 1) != 0)) {
                die_fmt("pop requires even register", r);
            }
            word = enc_op6_rd(op6, (uint8_t)rd);
        } else if (!strcmp(inst, "ret")) {
            word = 0x7410;
        } else if (!strcmp(inst, "halt")) {
            word = 0xFFFF;
        } else if (!strcmp(inst, "mvi")) {
            char *r = next_token(&ctx);
            if (!r) die("Missing register");
            char *imm = next_token(&ctx);
            if (!imm) die("Missing immediate");
            int rd = parse_reg(r);
            int iv = parse_int(imm, -128, 255);
            word = enc_op4_reg_imm8(0xC, (uint8_t)rd, (uint8_t)iv);
        } else if (!strcmp(inst, "jz")) {
            char *off = next_token(&ctx);
            if (!off) die("Missing offset");
            char *r = next_token(&ctx);
            if (!r) die("Missing register");
            int rs = parse_reg(r);

            if (is_valid_label_name(off)) {
                word = enc_op4_reg_imm8(0xD, (uint8_t)rs, 0);
                codevec_push(&code, word);
                fixupvec_push(&fixups, (int)(code.size - 1), off, g_line_num, FIX_IMM8, (uint8_t)rs, 0xD);
                instr_index++;
                emit = 0;
            } else {
                int iv = parse_int(off, -128, 127);
                word = enc_op4_reg_imm8(0xD, (uint8_t)rs, (uint8_t)iv);
            }
        } else if (!strcmp(inst, "calr") || !strcmp(inst, "call") || !strcmp(inst, "jr")) {
            uint8_t op4 = (!strcmp(inst, "jr")) ? 0xF : 0xE;
            char *off = next_token(&ctx);
            if (!off) die("Missing offset");

            if (is_valid_label_name(off)) {
                word = enc_op4_rel12(op4, 0);
                codevec_push(&code, word);
                fixupvec_push(&fixups, (int)(code.size - 1), off, g_line_num, FIX_REL12, 0, op4);
                instr_index++;
                emit = 0;
            } else {
                int iv = parse_int(off, -2048, 2047);
                word = enc_op4_rel12(op4, (uint16_t)iv);
            }
        } else if (!strcmp(inst, "stm")) {
            char *m = next_token(&ctx);
            if (!m) die("Missing memory operand");
            char *r = next_token(&ctx);
            if (!r) die("Missing register operand");

            int is_x = 0;
            int iv = 0;
            parse_memref(m, &is_x, &iv);
            int rs = parse_reg(r);
            word = enc_op4_reg_imm8(is_x ? 0x8 : 0xA, (uint8_t)rs, (uint8_t)iv);
        } else if (!strcmp(inst, "ldm")) {
            char *r = next_token(&ctx);
            if (!r) die("Missing register operand");
            char *m = next_token(&ctx);
            if (!m) die("Missing memory operand");

            int is_x = 0;
            int iv = 0;
            parse_memref(m, &is_x, &iv);
            int rd = parse_reg(r);
            word = enc_op4_reg_imm8(is_x ? 0x9 : 0xB, (uint8_t)rd, (uint8_t)iv);
        } else {
            die_fmt("Unknown instruction", inst);
        }

        if (emit) {
            codevec_push(&code, word);
            instr_index++;
        }
    }

    for (size_t i = 0; i < fixups.size; i++) {
        Fixup *f = &fixups.data[i];
        int tgt = symbol_find(&symbols, f->name);
        if (tgt < 0) {
            g_line_num = f->line_num;
            snprintf(g_line, sizeof(g_line), "%s", f->name);
            die_fmt("Undefined label", f->name);
        }

        int rel = tgt - (f->index + 1);
        if (f->kind == FIX_IMM8) {
            if (rel < -128 || rel > 127) {
                g_line_num = f->line_num;
                die("8-bit relative offset out of range");
            }
            code.data[f->index] = enc_op4_reg_imm8(f->op_nibble, f->reg_nibble, (uint8_t)rel);
        } else {
            if (rel < -2048 || rel > 2047) {
                g_line_num = f->line_num;
                die("12-bit relative offset out of range");
            }
            code.data[f->index] = enc_op4_rel12(f->op_nibble, (uint16_t)rel);
        }
    }

    for (size_t i = 0; i < code.size; i++) {
        printf("%04X\n", code.data[i]);
    }

    return EXIT_SUCCESS;
}
