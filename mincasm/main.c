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
    FIX_REL16,
    FIX16_IMM8,  // minc-16 jz/jnz: [17:12]=op6, [11:4]=imm8, [3:0]=rs
    FIX16_REL16  // minc-16 calr/jr: [17:16]=op2, [15:0]=off16 (contiguous)
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
    uint32_t *data;
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
    INST_8_ALU_RR,
    INST_8_REG,
    INST_8_FIXED,
    INST_8_MVI,
    INST_8_JZ,
    INST_8_REL16,
    INST_8_MEM_STORE,
    INST_8_MEM_LOAD,
    /* minc-16 (`mincasm -16`). Different ISA, so a separate spec table and a
     * separate set of encoders -- see Hardware.md "### minc-16". */
    INST_16_ALU_RR,
    INST_16_IMM,
    INST_16_MEM_ST,
    INST_16_MEM_LD,
    INST_16_BR,
    INST_16_STK_REG,
    INST_16_FIXED,
    INST_16_REL16
} InstKind;

enum {
    INST_FLAG_EVEN_REG = 1u << 0
};

typedef struct {
    const char *mnemonic;
    InstKind kind;
    uint8_t opcode_a;
    uint8_t opcode_b;
    uint8_t opcode_c;
    uint32_t fixed_word;
    uint8_t flags;
} InstSpec;

static const InstSpec g_inst_specs[] = {
    {"mov",  INST_8_ALU_RR,    0x00, 0x00, 0x00, 0x00000, 0},
    {"or",   INST_8_ALU_RR,    0x01, 0x00, 0x00, 0x00000, 0},
    {"and",  INST_8_ALU_RR,    0x02, 0x00, 0x00, 0x00000, 0},
    {"xor",  INST_8_ALU_RR,    0x03, 0x00, 0x00, 0x00000, 0},
    {"add",  INST_8_ALU_RR,    0x04, 0x00, 0x00, 0x00000, 0},
    {"adc",  INST_8_ALU_RR,    0x05, 0x00, 0x00, 0x00000, 0},
    {"sub",  INST_8_ALU_RR,    0x06, 0x00, 0x00, 0x00000, 0},
    {"sbc",  INST_8_ALU_RR,    0x07, 0x00, 0x00, 0x00000, 0},
    {"chz",  INST_8_ALU_RR,    0x08, 0x00, 0x00, 0x00000, 0},
    {"lt",   INST_8_ALU_RR,    0x0A, 0x00, 0x00, 0x00000, 0},
    {"ltc",  INST_8_ALU_RR,    0x0B, 0x00, 0x00, 0x00000, 0},
    {"rr",   INST_8_ALU_RR,    0x0C, 0x00, 0x00, 0x00000, 0},
    {"mul",  INST_8_ALU_RR,    0x0E, 0x00, 0x00, 0x00000, 0},
    {"mulh", INST_8_ALU_RR,    0x0F, 0x00, 0x00, 0x00000, 0},
    {"push", INST_8_REG,       0x1C, 0x00, 0x00, 0x00000, 0},
    {"pop",  INST_8_REG,       0x1D, 0x00, 0x00, 0x00000, 0},
    {"ret",  INST_8_FIXED,     0x00, 0x00, 0x00, 0x1F000, 0},
    {"reti", INST_8_FIXED,     0x00, 0x00, 0x00, 0x1E000, 0},
    {"halt", INST_8_FIXED,     0x00, 0x00, 0x00, 0x3FFFF, 0},
    {"mvi",  INST_8_MVI,       0x0E, 0x00, 0x00, 0x00000, 0},
    {"jz",   INST_8_JZ,        0x0C, 0x00, 0x00, 0x00000, 0},
    {"calr", INST_8_REL16,     0x02, 0x00, 0x00, 0x00000, 0},
    {"jr",   INST_8_REL16,     0x03, 0x00, 0x00, 0x00000, 0},
    {"stm",  INST_8_MEM_STORE, 0x10, 0x12, 0x14, 0x00000, 0},
    {"ldm",  INST_8_MEM_LOAD,  0x11, 0x13, 0x15, 0x00000, 0},
};

/* minc-16 instruction table (selected by `mincasm -16`).
 * opcode_a = subop / op6 / op2 / stack ext, depending on kind.
 * opcode_b = byte(1) vs word(0) for the memory kinds. */
static const InstSpec g_inst_specs16[] = {
    {"mov",  INST_16_ALU_RR,  0x00, 0x00, 0x00, 0x00000, 0},
    {"or",   INST_16_ALU_RR,  0x01, 0x00, 0x00, 0x00000, 0},
    {"and",  INST_16_ALU_RR,  0x02, 0x00, 0x00, 0x00000, 0},
    {"xor",  INST_16_ALU_RR,  0x03, 0x00, 0x00, 0x00000, 0},
    {"add",  INST_16_ALU_RR,  0x04, 0x00, 0x00, 0x00000, 0},
    {"adc",  INST_16_ALU_RR,  0x05, 0x00, 0x00, 0x00000, 0},
    {"sub",  INST_16_ALU_RR,  0x06, 0x00, 0x00, 0x00000, 0},
    {"sbc",  INST_16_ALU_RR,  0x07, 0x00, 0x00, 0x00000, 0},
    {"chz",  INST_16_ALU_RR,  0x08, 0x00, 0x00, 0x00000, 0},
    {"sxb",  INST_16_ALU_RR,  0x09, 0x00, 0x00, 0x00000, 0},
    {"lt",   INST_16_ALU_RR,  0x0A, 0x00, 0x00, 0x00000, 0},
    {"ltc",  INST_16_ALU_RR,  0x0B, 0x00, 0x00, 0x00000, 0},
    {"rr",   INST_16_ALU_RR,  0x0C, 0x00, 0x00, 0x00000, 0},
    {"asr",  INST_16_ALU_RR,  0x0D, 0x00, 0x00, 0x00000, 0},
    {"mul",  INST_16_ALU_RR,  0x0E, 0x00, 0x00, 0x00000, 0},
    {"mulh", INST_16_ALU_RR,  0x0F, 0x00, 0x00, 0x00000, 0},
    {"mvi",  INST_16_IMM,     0x00, 0x00, 0x00, 0x00000, 0},
    {"mvih", INST_16_IMM,     0x01, 0x00, 0x00, 0x00000, 0},
    {"addi", INST_16_IMM,     0x02, 0x00, 0x00, 0x00000, 0},
    {"stw",  INST_16_MEM_ST,  0x00, 0x00, 0x00, 0x00000, 0},
    {"stb",  INST_16_MEM_ST,  0x00, 0x01, 0x00, 0x00000, 0},
    {"ldw",  INST_16_MEM_LD,  0x00, 0x00, 0x00, 0x00000, 0},
    {"ldb",  INST_16_MEM_LD,  0x00, 0x01, 0x00, 0x00000, 0},
    {"jz",   INST_16_BR,      0x0C, 0x00, 0x00, 0x00000, 0},
    {"jnz",  INST_16_BR,      0x0D, 0x00, 0x00, 0x00000, 0},
    {"push", INST_16_STK_REG, 0x00, 0x00, 0x00, 0x00000, 0},
    {"pop",  INST_16_STK_REG, 0x01, 0x00, 0x00, 0x00000, 0},
    {"ret",  INST_16_FIXED,   0x00, 0x00, 0x00, 0x0E200, 0},
    {"reti", INST_16_FIXED,   0x00, 0x00, 0x00, 0x0E300, 0},
    {"halt", INST_16_FIXED,   0x00, 0x00, 0x00, 0x3FFFF, 0},
    {"calr", INST_16_REL16,   0x02, 0x00, 0x00, 0x00000, 0},
    {"jr",   INST_16_REL16,   0x03, 0x00, 0x00, 0x00000, 0},
};

/* 0 = minc-8 (default), 1 = minc-16 (`-16`). */
static int g_m16 = 0;

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

static void codevec_push(CodeVec *v, uint32_t x) {
    if (v->size == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 128;
        uint32_t *nd = (uint32_t *)realloc(v->data, ncap * sizeof(uint32_t));
        if (!nd) {
            die_oom();
        }
        v->data = nd;
        v->cap = ncap;
    }
    v->data[v->size++] = x;
}

/* Grows the code vector with zero-filled words, if needed, so that
 * v->size >= addr + 1, i.e. v->data[addr] is a valid slot. */
static void codevec_reserve_addr(CodeVec *v, size_t addr) {
    while (v->size <= addr) {
        codevec_push(v, 0);
    }
}

/* Writes a word at an explicit address (the current .org location counter).
 * addr may fall before the current end of the buffer (a backward .org),
 * in which case the existing word there is overwritten; otherwise the gap
 * is zero-filled up to addr. */
static void emit_at(CodeVec *v, size_t addr, uint32_t word) {
    codevec_reserve_addr(v, addr);
    v->data[addr] = word;
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
    const InstSpec *table = g_m16 ? g_inst_specs16 : g_inst_specs;
    size_t count = g_m16 ? sizeof(g_inst_specs16) / sizeof(g_inst_specs16[0])
                         : sizeof(g_inst_specs)   / sizeof(g_inst_specs[0]);

    hashmap_init(map);
    hashmap_rehash(map, count * 2);

    for (size_t i = 0; i < count; i++) {
        const InstSpec *spec = &table[i];
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

static uint32_t enc_op6_rr(uint8_t op6, uint8_t rd, uint8_t rs) {
    return (uint32_t)(((uint32_t)op6 << 10) | ((uint32_t)rd << 4) | rs);
}

static uint32_t enc_op6_rd(uint8_t op6, uint8_t rd) {
    return (uint32_t)(((uint32_t)op6 << 12) | ((uint32_t)rd << 4));
}

static uint32_t enc_op6_reg_imm8(uint8_t op4, uint8_t reg, uint8_t imm8) {
    return (uint32_t)(((uint32_t)op4 << 12) | ((uint32_t)(imm8 >> 4) << 8) | ((uint32_t)reg << 4) | (imm8 & 0x0F));
}

static uint32_t enc_op2_rel16(uint8_t op2, uint32_t off16) {
    uint8_t n3 = (off16 >> 12) & 0xF;
    uint8_t n2 = (off16 >>  8) & 0xF;
    uint8_t n1 = (off16 >>  4) & 0xF;
    uint8_t n0 =  off16        & 0xF;
    return ((uint32_t)op2 << 16) | ((uint32_t)n3 << 12)
         | ((uint32_t)n1 <<  8) | ((uint32_t)n2 <<  4) | n0;
}

/* ---- minc-16 encoders (see Hardware.md "#### 命令フォーマット") ---- */

/* [17:14]=0000 [13:10]=subop [9:8]=00 [7:4]=rd [3:0]=rs */
static uint32_t enc16_alu(uint8_t subop, uint8_t rd, uint8_t rs) {
    return ((uint32_t)subop << 10) | ((uint32_t)rd << 4) | rs;
}

/* [17:14]=0001 [13:12]=subop [11:8]=n[7:4] [7:4]=rd [3:0]=n[3:0].
 * The immediate is split so that rd stays at [7:4] -- `addi` reads rd as the
 * ALU A operand, which must come off port A. */
static uint32_t enc16_imm(uint8_t subop, uint8_t rd, uint8_t imm8) {
    return (1u << 14) | ((uint32_t)subop << 12)
         | ((uint32_t)(imm8 >> 4) << 8) | ((uint32_t)rd << 4) | (imm8 & 0x0F);
}

/* [17:14]=0010 [13]=ld [12]=byte [11:4]=abs8 [3:0]=reg */
static uint32_t enc16_abs(int is_ld, int is_byte, uint8_t abs8, uint8_t reg) {
    return (2u << 14) | ((uint32_t)(is_ld != 0) << 13) | ((uint32_t)(is_byte != 0) << 12)
         | ((uint32_t)abs8 << 4) | reg;
}

/* [17:16]=01 [15]=ld [14]=byte [13:8]=simm6 [7:4]=base [3:0]=reg */
static uint32_t enc16_disp(int is_ld, int is_byte, int disp, uint8_t base, uint8_t reg) {
    return (1u << 16) | ((uint32_t)(is_ld != 0) << 15) | ((uint32_t)(is_byte != 0) << 14)
         | ((uint32_t)(disp & 0x3F) << 8) | ((uint32_t)base << 4) | reg;
}

/* [17:12]=op6 [11:4]=imm8 [3:0]=rs */
static uint32_t enc16_br(uint8_t op6, uint8_t imm8, uint8_t rs) {
    return ((uint32_t)op6 << 12) | ((uint32_t)imm8 << 4) | rs;
}

/* [17:12]=001110 [11:8]=ext [7:4]=0000 [3:0]=reg */
static uint32_t enc16_stk(uint8_t ext, uint8_t reg) {
    return (0x0Eu << 12) | ((uint32_t)ext << 8) | reg;
}

/* [17:16]=op2 [15:0]=off16 (contiguous, unlike minc-8's scattered nibbles) */
static uint32_t enc16_rel16(uint8_t op2, uint32_t off16) {
    return ((uint32_t)op2 << 16) | (off16 & 0xFFFF);
}

/* minc-16 memory operand: "[rB+n]" / "[rB-n]" / "[rB]" -> displacement mode,
 * a bare integer -> 8-bit absolute mode. No spaces allowed inside the brackets
 * (next_token() splits on whitespace). */
static void parse_memref16(char *tok, int *is_disp, int *base, int *disp) {
    if (tok[0] == '[') {
        char *end = strchr(tok, ']');
        if (!end || end[1] != '\0') {
            die_fmt("Malformed memory operand", tok);
        }
        *end = '\0';
        char *inner = tok + 1;
        char *sign = strpbrk(inner, "+-");
        *is_disp = 1;
        if (sign) {
            char saved = *sign;
            *sign = '\0';
            *base = parse_reg(inner);
            *sign = saved;
            *disp = parse_int(sign, -32, 31);
        } else {
            *base = parse_reg(inner);
            *disp = 0;
        }
        return;
    }
    *is_disp = 0;
    *base = 0;
    *disp = parse_int(tok, 0, 255);
}

static void parse_memref(char *tok, int *addr_mode, int *imm8) {
    if ((tok[0] == 'X' || tok[0] == 'x') && (tok[1] == '+' || tok[1] == '-')) {
        *addr_mode = 0;
        *imm8 = parse_int(tok + 1, -128, 127);
        return;
    }
    if ((tok[0] == 'Y' || tok[0] == 'y') && (tok[1] == '+' || tok[1] == '-')) {
        *addr_mode = 1;
        *imm8 = parse_int(tok + 1, -128, 127);
        return;
    }
    *addr_mode = 2;
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

static void emit_fixup(CodeVec *code, FixupVec *fixups, const char *name, int line_num, FixKind kind, uint8_t reg_nibble, uint8_t op_nibble, uint32_t placeholder, size_t addr) {
    emit_at(code, addr, placeholder);
    fixupvec_push(fixups, addr, name, line_num, kind, reg_nibble, op_nibble);
}

int main(int argc, char **argv) {
    CodeVec code = {0};
    FixupVec fixups = {0};
    HashMap labels;
    HashMap inst_map;
    size_t cur_addr = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-16") == 0 || strcmp(argv[i], "--m16") == 0) {
            g_m16 = 1;
        } else {
            fprintf(stderr, "Usage: mincasm [-16] < input.asm > output.hex\n");
            fprintf(stderr, "  -16, --m16   assemble for minc-16 instead of minc-8\n");
            return EXIT_FAILURE;
        }
    }

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
                labelmap_put(&labels, p, cur_addr);
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

        if (strcmp(inst, ".org") == 0) {
            char *addr_tok = next_token(&ctx);
            if (!addr_tok) {
                die("Missing address for .org");
            }
            cur_addr = (size_t)parse_int(addr_tok, 0, 65535);
            continue;
        }

        const InstSpec *spec = instruction_lookup(&inst_map, inst);
        if (!spec) {
            die_fmt("Unknown instruction", inst);
        }

        uint32_t word = 0;
        int emit = 1;

        switch (spec->kind) {
            case INST_8_ALU_RR: {
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
            case INST_8_REG: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing operand");
                }

                int rd = parse_reg(r);
                if ((spec->flags & INST_FLAG_EVEN_REG) && ((rd & 1) != 0)) {
                    die_fmt("push and pop requires even register", r);
                }
                word = enc_op6_rd(spec->opcode_a, (uint8_t)rd);
                break;
            }
            case INST_8_FIXED:
                word = spec->fixed_word;
                break;
            case INST_8_MVI: {
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
                word = enc_op6_reg_imm8(spec->opcode_a, (uint8_t)rd, (uint8_t)iv);
                break;
            }
            case INST_8_JZ: {
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
                    emit_fixup(&code, &fixups, off, g_line_num, FIX_IMM8, (uint8_t)rs, spec->opcode_a, enc_op6_reg_imm8(spec->opcode_a, (uint8_t)rs, 0), cur_addr);
                    emit = 0;
                } else {
                    int iv = parse_int(off, -128, 127);
                    word = enc_op6_reg_imm8(spec->opcode_a, (uint8_t)rs, (uint8_t)iv);
                }
                break;
            }
            case INST_8_REL16: {
                char *off = next_token(&ctx);
                if (!off) {
                    die("Missing offset");
                }

                if (is_valid_label_name(off)) {
                    emit_fixup(&code, &fixups, off, g_line_num, FIX_REL16, 0, spec->opcode_a, enc_op2_rel16(spec->opcode_a, 0), cur_addr);
                    emit = 0;
                } else {
                    int iv = parse_int(off, -32768, 32767);
                    word = enc_op2_rel16(spec->opcode_a, (uint32_t)iv);
                }
                break;
            }
            case INST_8_MEM_STORE: {
                char *m = next_token(&ctx);
                if (!m) {
                    die("Missing memory operand");
                }
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register operand");
                }

                int addr_mode = 0;
                int iv = 0;
                parse_memref(m, &addr_mode, &iv);
                int rs = parse_reg(r);
                uint8_t op = (addr_mode == 0) ? spec->opcode_a :
                             (addr_mode == 1) ? spec->opcode_b : spec->opcode_c;
                word = enc_op6_reg_imm8(op, (uint8_t)rs, (uint8_t)iv);
                break;
            }
            case INST_8_MEM_LOAD: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register operand");
                }
                char *m = next_token(&ctx);
                if (!m) {
                    die("Missing memory operand");
                }

                int addr_mode = 0;
                int iv = 0;
                parse_memref(m, &addr_mode, &iv);
                int rd = parse_reg(r);
                uint8_t op = (addr_mode == 0) ? spec->opcode_a :
                             (addr_mode == 1) ? spec->opcode_b : spec->opcode_c;
                word = enc_op6_reg_imm8(op, (uint8_t)rd, (uint8_t)iv);
                break;
            }

            case INST_16_ALU_RR: {
                char *r0 = next_token(&ctx);
                if (!r0) {
                    die("Missing register operand");
                }
                char *r1 = next_token(&ctx);
                if (!r1) {
                    die("Missing register operand");
                }
                word = enc16_alu(spec->opcode_a, (uint8_t)parse_reg(r0), (uint8_t)parse_reg(r1));
                break;
            }
            case INST_16_IMM: {
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
                word = enc16_imm(spec->opcode_a, (uint8_t)rd, (uint8_t)iv);
                break;
            }
            case INST_16_MEM_ST: {
                char *m = next_token(&ctx);
                if (!m) {
                    die("Missing memory operand");
                }
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register operand");
                }
                int is_disp = 0, base = 0, dv = 0;
                parse_memref16(m, &is_disp, &base, &dv);
                int rs = parse_reg(r);
                word = is_disp ? enc16_disp(0, spec->opcode_b, dv, (uint8_t)base, (uint8_t)rs)
                               : enc16_abs(0, spec->opcode_b, (uint8_t)dv, (uint8_t)rs);
                break;
            }
            case INST_16_MEM_LD: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register operand");
                }
                char *m = next_token(&ctx);
                if (!m) {
                    die("Missing memory operand");
                }
                int is_disp = 0, base = 0, dv = 0;
                parse_memref16(m, &is_disp, &base, &dv);
                int rd = parse_reg(r);
                word = is_disp ? enc16_disp(1, spec->opcode_b, dv, (uint8_t)base, (uint8_t)rd)
                               : enc16_abs(1, spec->opcode_b, (uint8_t)dv, (uint8_t)rd);
                break;
            }
            case INST_16_BR: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register");
                }
                char *off = next_token(&ctx);
                if (!off) {
                    die("Missing offset");
                }
                int rs = parse_reg(r);

                if (is_valid_label_name(off)) {
                    emit_fixup(&code, &fixups, off, g_line_num, FIX16_IMM8, (uint8_t)rs,
                               spec->opcode_a, enc16_br(spec->opcode_a, 0, (uint8_t)rs), cur_addr);
                    emit = 0;
                } else {
                    int iv = parse_int(off, -128, 127);
                    word = enc16_br(spec->opcode_a, (uint8_t)iv, (uint8_t)rs);
                }
                break;
            }
            case INST_16_STK_REG: {
                char *r = next_token(&ctx);
                if (!r) {
                    die("Missing register");
                }
                word = enc16_stk(spec->opcode_a, (uint8_t)parse_reg(r));
                break;
            }
            case INST_16_FIXED:
                word = spec->fixed_word;
                break;
            case INST_16_REL16: {
                char *off = next_token(&ctx);
                if (!off) {
                    die("Missing offset");
                }

                if (is_valid_label_name(off)) {
                    emit_fixup(&code, &fixups, off, g_line_num, FIX16_REL16, 0,
                               spec->opcode_a, enc16_rel16(spec->opcode_a, 0), cur_addr);
                    emit = 0;
                } else {
                    int iv = parse_int(off, -32768, 32767);
                    word = enc16_rel16(spec->opcode_a, (uint32_t)iv);
                }
                break;
            }
        }

        if (emit) {
            emit_at(&code, cur_addr, word);
        }
        cur_addr++;
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
            code.data[f->index] = enc_op6_reg_imm8(f->op_nibble, f->reg_nibble, (uint8_t)rel);
        } else if (f->kind == FIX16_IMM8) {
            if (rel < -128 || rel > 127) {
                g_line_num = f->line_num;
                snprintf(g_line, sizeof(g_line), "%s", f->name);
                die("8-bit relative offset out of range");
            }
            code.data[f->index] = enc16_br(f->op_nibble, (uint8_t)rel, f->reg_nibble);
        } else if (f->kind == FIX16_REL16) {
            if (rel < -32768 || rel > 32767) {
                g_line_num = f->line_num;
                snprintf(g_line, sizeof(g_line), "%s", f->name);
                die("16-bit relative offset out of range");
            }
            code.data[f->index] = enc16_rel16(f->op_nibble, (uint32_t)rel);
        } else {
            if (rel < -32768 || rel > 32767) {
                g_line_num = f->line_num;
                snprintf(g_line, sizeof(g_line), "%s", f->name);
                die("16-bit relative offset out of range");
            }
            code.data[f->index] = enc_op2_rel16(f->op_nibble, (uint32_t)rel);
        }
    }

    for (size_t i = 0; i < code.size; i++) {
        printf("%05X\n", code.data[i]);
    }

    fixupvec_destroy(&fixups);
    hashmap_destroy(&labels, 1);
    hashmap_destroy(&inst_map, 0);
    free(code.data);

    return EXIT_SUCCESS;
}
