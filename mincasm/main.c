#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512
#define HASHMAP_MIN_CAP 16
#define HASHMAP_MAX_LOAD_NUM 7
#define HASHMAP_MAX_LOAD_DEN 10

typedef enum {
    FIX_IMM8,
    FIX_REL12
} FixKind;

typedef struct {
    size_t index;
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
    Fixup *data;
    size_t size;
    size_t cap;
} FixupVec;

typedef struct {
    const char *key;
    uint64_t hash;
    uintptr_t value;
    int used;
} HashEntry;

typedef struct {
    HashEntry *entries;
    size_t size;
    size_t cap;
} HashMap;

typedef enum {
    INST_ALU_RR,
    INST_REG,
    INST_FIXED,
    INST_FLAG_IMM,
    INST_MVI,
    INST_JZ,
    INST_REL12,
    INST_MEM_STORE,
    INST_MEM_LOAD
} InstKind;

enum {
    INST_FLAG_EVEN_REG = 1u << 0
};

typedef struct {
    const char *mnemonic;
    InstKind kind;
    uint8_t opcode_a;
    uint8_t opcode_b;
    uint16_t fixed_word;
    uint8_t flags;
} InstSpec;

static const InstSpec g_inst_specs[] = {
    {"mov", INST_ALU_RR, 0x00, 0x00, 0x0000, 0},
    {"or", INST_ALU_RR, 0x01, 0x00, 0x0000, 0},
    {"and", INST_ALU_RR, 0x02, 0x00, 0x0000, 0},
    {"xor", INST_ALU_RR, 0x03, 0x00, 0x0000, 0},
    {"add", INST_ALU_RR, 0x04, 0x00, 0x0000, 0},
    {"adc", INST_ALU_RR, 0x05, 0x00, 0x0000, 0},
    {"sub", INST_ALU_RR, 0x06, 0x00, 0x0000, 0},
    {"sbc", INST_ALU_RR, 0x07, 0x00, 0x0000, 0},
    {"lt", INST_ALU_RR, 0x08, 0x00, 0x0000, 0},
    {"ltc", INST_ALU_RR, 0x09, 0x00, 0x0000, 0},
    {"rr", INST_ALU_RR, 0x0A, 0x00, 0x0000, 0},
    {"mul", INST_ALU_RR, 0x0E, 0x00, 0x0000, 0},
    {"mulh", INST_ALU_RR, 0x0F, 0x00, 0x0000, 0},
    {"stf", INST_FLAG_IMM, 0x10, 0x00, 0x0000, 0},
    {"clf", INST_FLAG_IMM, 0x11, 0x00, 0x0000, 0},
    {"push", INST_REG, 0x1C, 0x00, 0x0000, 0},
    {"pop", INST_REG, 0x1D, 0x00, 0x0000, INST_FLAG_EVEN_REG},
    {"sts", INST_REG, 0x1E, 0x00, 0x0000, 0},
    {"lds", INST_REG, 0x1F, 0x00, 0x0000, 0},
    {"ret", INST_FIXED, 0x00, 0x00, 0x7410, 0},
    {"halt", INST_FIXED, 0x00, 0x00, 0xFFFF, 0},
    {"mvi", INST_MVI, 0x0C, 0x00, 0x0000, 0},
    {"jz", INST_JZ, 0x0D, 0x00, 0x0000, 0},
    {"calr", INST_REL12, 0x0E, 0x00, 0x0000, 0},
    {"jr", INST_REL12, 0x0F, 0x00, 0x0000, 0},
    {"stm", INST_MEM_STORE, 0x08, 0x0A, 0x0000, 0},
    {"ldm", INST_MEM_LOAD, 0x09, 0x0B, 0x0000, 0},
};

static int g_line_num;
static char g_line[MAX_LINE];

static void die_oom(void) {
    fprintf(stderr, "Error: out of memory\n");
    exit(EXIT_FAILURE);
}

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
        die_oom();
    }
    memcpy(p, s, n);
    return p;
}

static uint64_t hash_str(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (uint64_t)(unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static size_t next_pow2(size_t n) {
    size_t cap = HASHMAP_MIN_CAP;
    while (cap < n) {
        cap <<= 1;
    }
    return cap;
}

static void codevec_push(CodeVec *v, uint16_t x) {
    if (v->size == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 128;
        uint16_t *nd = (uint16_t *)realloc(v->data, ncap * sizeof(uint16_t));
        if (!nd) {
            die_oom();
        }
        v->data = nd;
        v->cap = ncap;
    }
    v->data[v->size++] = x;
}

static void fixupvec_push(FixupVec *v, size_t index, const char *name, int line_num, FixKind kind, uint8_t reg_nibble, uint8_t op_nibble) {
    if (v->size == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 64;
        Fixup *nd = (Fixup *)realloc(v->data, ncap * sizeof(Fixup));
        if (!nd) {
            die_oom();
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

static void fixupvec_destroy(FixupVec *v) {
    for (size_t i = 0; i < v->size; i++) {
        free(v->data[i].name);
    }
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->cap = 0;
}

static void hashmap_init(HashMap *map) {
    map->entries = NULL;
    map->size = 0;
    map->cap = 0;
}

static void hashmap_destroy(HashMap *map, int free_keys) {
    if (map->entries && free_keys) {
        for (size_t i = 0; i < map->cap; i++) {
            if (map->entries[i].used) {
                free((char *)map->entries[i].key);
            }
        }
    }
    free(map->entries);
    map->entries = NULL;
    map->size = 0;
    map->cap = 0;
}

static void hashmap_insert_raw(HashMap *map, const char *key, uint64_t hash, uintptr_t value) {
    size_t mask = map->cap - 1;
    size_t idx = (size_t)hash & mask;
    while (map->entries[idx].used) {
        idx = (idx + 1) & mask;
    }
    map->entries[idx].key = key;
    map->entries[idx].hash = hash;
    map->entries[idx].value = value;
    map->entries[idx].used = 1;
    map->size++;
}

static const HashEntry *hashmap_lookup_entry(const HashMap *map, const char *key, uint64_t hash) {
    if (map->cap == 0) {
        return NULL;
    }

    size_t mask = map->cap - 1;
    size_t idx = (size_t)hash & mask;
    for (;;) {
        const HashEntry *entry = &map->entries[idx];
        if (!entry->used) {
            return NULL;
        }
        if (entry->hash == hash && strcmp(entry->key, key) == 0) {
            return entry;
        }
        idx = (idx + 1) & mask;
    }
}

static void hashmap_rehash(HashMap *map, size_t min_cap) {
    size_t ncap = next_pow2(min_cap);
    if (ncap < HASHMAP_MIN_CAP) {
        ncap = HASHMAP_MIN_CAP;
    }

    HashEntry *new_entries = (HashEntry *)calloc(ncap, sizeof(HashEntry));
    if (!new_entries) {
        die_oom();
    }

    HashEntry *old_entries = map->entries;
    size_t old_cap = map->cap;

    map->entries = new_entries;
    map->cap = ncap;
    map->size = 0;

    if (old_entries) {
        size_t mask = ncap - 1;
        for (size_t i = 0; i < old_cap; i++) {
            if (!old_entries[i].used) {
                continue;
            }
            size_t idx = (size_t)old_entries[i].hash & mask;
            while (new_entries[idx].used) {
                idx = (idx + 1) & mask;
            }
            new_entries[idx] = old_entries[i];
            map->size++;
        }
        free(old_entries);
    }
}

static void hashmap_ensure_capacity(HashMap *map) {
    if (map->cap == 0) {
        hashmap_rehash(map, HASHMAP_MIN_CAP);
        return;
    }
    if ((map->size + 1) * HASHMAP_MAX_LOAD_DEN >= map->cap * HASHMAP_MAX_LOAD_NUM) {
        hashmap_rehash(map, map->cap * 2);
    }
}

static void labelmap_put(HashMap *map, const char *name, size_t address) {
    uint64_t hash = hash_str(name);
    if (hashmap_lookup_entry(map, name, hash)) {
        die_fmt("Duplicate label", name);
    }
    hashmap_ensure_capacity(map);
    hashmap_insert_raw(map, dupstr(name), hash, (uintptr_t)address);
}

static int labelmap_get(const HashMap *map, const char *name, size_t *address) {
    uint64_t hash = hash_str(name);
    const HashEntry *entry = hashmap_lookup_entry(map, name, hash);
    if (!entry) {
        return 0;
    }
    *address = (size_t)entry->value;
    return 1;
}

static void instruction_map_init(HashMap *map) {
    hashmap_init(map);
    hashmap_rehash(map, sizeof(g_inst_specs) / sizeof(g_inst_specs[0]) * 2);

    for (size_t i = 0; i < sizeof(g_inst_specs) / sizeof(g_inst_specs[0]); i++) {
        const InstSpec *spec = &g_inst_specs[i];
        uint64_t hash = hash_str(spec->mnemonic);
        if (hashmap_lookup_entry(map, spec->mnemonic, hash)) {
            fprintf(stderr, "Error: Duplicate mnemonic '%s'\n", spec->mnemonic);
            exit(EXIT_FAILURE);
        }
        hashmap_insert_raw(map, spec->mnemonic, hash, (uintptr_t)spec);
    }
}

static const InstSpec *instruction_lookup(const HashMap *map, const char *mnemonic) {
    uint64_t hash = hash_str(mnemonic);
    const HashEntry *entry = hashmap_lookup_entry(map, mnemonic, hash);
    return entry ? (const InstSpec *)entry->value : NULL;
}

static char *lskip(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
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
    if (!s || *s == '\0') {
        return 0;
    }
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
    if (end == tok + 1 || *end != '\0' || v < 0 || v > 15) {
        die_fmt("Register out of range", tok);
    }
    return (int)v;
}

static int parse_int(const char *tok, int min_v, int max_v) {
    char *end = NULL;
    long v = strtol(tok, &end, 0);
    if (end == tok || *end != '\0') {
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

static void parse_memref(char *tok, int *is_x, int *imm8) {
    if ((tok[0] == 'X' || tok[0] == 'x' || tok[0] == 'Y' || tok[0] == 'y') && (tok[1] == '+' || tok[1] == '-')) {
        *is_x = (tok[0] == 'X' || tok[0] == 'x');
        *imm8 = parse_int(tok + 1, -128, 127);
        return;
    }
    *is_x = 0;
    *imm8 = parse_int(tok, -128, 127);
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

static void emit_fixup(CodeVec *code, FixupVec *fixups, const char *name, int line_num, FixKind kind, uint8_t reg_nibble, uint8_t op_nibble, uint16_t placeholder) {
    codevec_push(code, placeholder);
    fixupvec_push(fixups, code->size - 1, name, line_num, kind, reg_nibble, op_nibble);
}

int main(void) {
    CodeVec code = {0};
    FixupVec fixups = {0};
    HashMap labels;
    HashMap inst_map;

    hashmap_init(&labels);
    instruction_map_init(&inst_map);

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
                labelmap_put(&labels, p, code.size);
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

        const InstSpec *spec = instruction_lookup(&inst_map, inst);
        if (!spec) {
            die_fmt("Unknown instruction", inst);
        }

        uint16_t word = 0;
        int emit = 1;

        switch (spec->kind) {
            case INST_ALU_RR: {
                char *r0 = next_token(&ctx);
                if (!r0) {
                    die("Missing first operand");
                }
                char *r1 = next_token(&ctx);
                if (!r1) {
                    die("Missing second operand");
                }

                int rd = parse_reg(r0);
                int rs = parse_reg(r1);
                word = enc_op6_rr(spec->opcode_a, (uint8_t)rd, (uint8_t)rs);
                break;
            }
            case INST_REG: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing operand");
                }

                int rd = parse_reg(r);
                if ((spec->flags & INST_FLAG_EVEN_REG) && ((rd & 1) != 0)) {
                    die_fmt("pop requires even register", r);
                }
                word = enc_op6_rd(spec->opcode_a, (uint8_t)rd);
                break;
            }
            case INST_FIXED:
                word = spec->fixed_word;
                break;
            case INST_FLAG_IMM: {
                char *imm = next_token(&ctx);
                if (!imm) {
                    die("Missing immediate for stf/clf");
                }
                if (imm[0] == '#') {
                    imm++;
                }
                int c = parse_int(imm, 0, 1);
                word = (uint16_t)(((uint16_t)spec->opcode_a << 10) | (uint16_t)c);
                break;
            }
            case INST_MVI: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register");
                }
                char *imm = next_token(&ctx);
                if (!imm) {
                    die("Missing immediate");
                }
                int rd = parse_reg(r);
                int iv = parse_int(imm, -128, 255);
                word = enc_op4_reg_imm8(spec->opcode_a, (uint8_t)rd, (uint8_t)iv);
                break;
            }
            case INST_JZ: {
                char *off = next_token(&ctx);
                if (!off) {
                    die("Missing offset");
                }
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register");
                }
                int rs = parse_reg(r);

                if (is_valid_label_name(off)) {
                    emit_fixup(&code, &fixups, off, g_line_num, FIX_IMM8, (uint8_t)rs, spec->opcode_a, enc_op4_reg_imm8(spec->opcode_a, (uint8_t)rs, 0));
                    emit = 0;
                } else {
                    int iv = parse_int(off, -128, 127);
                    word = enc_op4_reg_imm8(spec->opcode_a, (uint8_t)rs, (uint8_t)iv);
                }
                break;
            }
            case INST_REL12: {
                char *off = next_token(&ctx);
                if (!off) {
                    die("Missing offset");
                }

                if (is_valid_label_name(off)) {
                    emit_fixup(&code, &fixups, off, g_line_num, FIX_REL12, 0, spec->opcode_a, enc_op4_rel12(spec->opcode_a, 0));
                    emit = 0;
                } else {
                    int iv = parse_int(off, -2048, 2047);
                    word = enc_op4_rel12(spec->opcode_a, (uint16_t)iv);
                }
                break;
            }
            case INST_MEM_STORE: {
                char *m = next_token(&ctx);
                if (!m) {
                    die("Missing memory operand");
                }
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register operand");
                }

                int is_x = 0;
                int iv = 0;
                parse_memref(m, &is_x, &iv);
                int rs = parse_reg(r);
                word = enc_op4_reg_imm8(is_x ? spec->opcode_a : spec->opcode_b, (uint8_t)rs, (uint8_t)iv);
                break;
            }
            case INST_MEM_LOAD: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register operand");
                }
                char *m = next_token(&ctx);
                if (!m) {
                    die("Missing memory operand");
                }

                int is_x = 0;
                int iv = 0;
                parse_memref(m, &is_x, &iv);
                int rd = parse_reg(r);
                word = enc_op4_reg_imm8(is_x ? spec->opcode_a : spec->opcode_b, (uint8_t)rd, (uint8_t)iv);
                break;
            }
        }

        if (emit) {
            codevec_push(&code, word);
        }
    }

    for (size_t i = 0; i < fixups.size; i++) {
        Fixup *f = &fixups.data[i];
        size_t tgt = 0;
        if (!labelmap_get(&labels, f->name, &tgt)) {
            g_line_num = f->line_num;
            snprintf(g_line, sizeof(g_line), "%s", f->name);
            die_fmt("Undefined label", f->name);
        }

        long rel = (long)tgt - (long)(f->index + 1);
        if (f->kind == FIX_IMM8) {
            if (rel < -128 || rel > 127) {
                g_line_num = f->line_num;
                snprintf(g_line, sizeof(g_line), "%s", f->name);
                die("8-bit relative offset out of range");
            }
            code.data[f->index] = enc_op4_reg_imm8(f->op_nibble, f->reg_nibble, (uint8_t)rel);
        } else {
            if (rel < -2048 || rel > 2047) {
                g_line_num = f->line_num;
                snprintf(g_line, sizeof(g_line), "%s", f->name);
                die("12-bit relative offset out of range");
            }
            code.data[f->index] = enc_op4_rel12(f->op_nibble, (uint16_t)rel);
        }
    }

    for (size_t i = 0; i < code.size; i++) {
        printf("%04X\n", code.data[i]);
    }

    fixupvec_destroy(&fixups);
    hashmap_destroy(&labels, 1);
    hashmap_destroy(&inst_map, 0);
    free(code.data);

    return EXIT_SUCCESS;
}
