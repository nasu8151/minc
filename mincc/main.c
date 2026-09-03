#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
#include "ast.h"
#include "codegen.h"
#include "codegen_16.h"
#include "errorhandle.h"

static void print_default_crt0(void) {
    printf("mvi r14,0\n");
    printf("mvi r15,0\n");
    printf("calr __on_entry\n");
    printf("calr main\n");
    printf("push r1\n");
    printf("push r0\n");
    printf("halt\n");
}

// minc-16 crt0. SP is the general-purpose r15 here (not a memory-mapped register
// pair), so zeroing it is a plain mvi and the first push lands at 0xFFFE; r14 is
// the frame pointer. main's result is one register, hence a single push.
static void print_default_crt0_16(void) {
    printf("mvi r15,0\n");
    printf("mvi r14,0\n");
    printf("calr __on_entry\n");
    printf("calr main\n");
    printf("push r0\n");
    printf("halt\n");
}

static void print_crt0(void) {
    if (g_m16) {
        print_default_crt0_16();
    } else {
        print_default_crt0();
    }
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-16") == 0 || strcmp(argv[i], "--m16") == 0) {
            g_m16 = 1;
        } else {
            fprintf(stderr, "Usage: mincc [-16] < input.c > output.asm\n");
            fprintf(stderr, "  -16, --m16   generate minc-16 assembly instead of minc-8\n");
            return EXIT_FAILURE;
        }
    }

    char line[256];
    char *code_src = calloc(1, 1); // renamed from `code` -- that name now collides with the global Node code[256]
    if (!code_src) {
        error("out of memory");
    }
    while (fgets(line, sizeof(line) - 1, stdin)) {
        line[sizeof(line) - 1] = '\0'; // null terminate
        char *new_code = (char *)realloc(code_src, strlen(code_src) + strlen(line) + 1);
        if (!new_code) {
            error("out of memory");
        }
        code_src = new_code;
        strcat(code_src, line);
    }
    fprintf(stderr, "Input code: %s\n", code_src);
    user_input = code_src;

    token = tokenize(user_input);

    long node_count = program(); // parse only -- code[] is now populated, nothing printed yet

    // Interrupt vectors (irq_in[0..3]) live at instruction addresses 0x0001-0x0004,
    // exactly where crt0's unlabeled preamble lands by default -- so crt0 only moves
    // (behind an .org-based vector table) when a [[isr=N]] function actually claims
    // one of those slots, keeping every non-interrupt program's output unchanged.
    long isr_owner[4] = { -1, -1, -1, -1 };
    bool have_isr_vector = false;
    for (long j = 0; j < node_count; j++) {
        Node *n = &code[j];
        if (n->type == ND_FUNC_DEF && n->valtype && n->valtype->type == TY_ISR && n->isr_vector >= 0) {
            isr_owner[n->isr_vector] = j;
            have_isr_vector = true;
        }
    }

    if (!have_isr_vector) {
        print_crt0(); // byte-for-byte identical to the previous unconditional crt0
    } else {
        printf(".org 0x0000\n");
        printf("jr __crt0_start\n");
        for (long v = 0; v < 4; v++) {
            printf(".org 0x%04lx\n", v + 1);
            if (isr_owner[v] != -1) {
                printf("jr %s\n", code[isr_owner[v]].name);
            } else {
                printf("reti\n"); // safe no-op resume for an unclaimed/spurious irq_in line
            }
        }
        printf(".org 0x0005\n");
        printf("__crt0_start:\n");
        // Interrupts stay masked out of reset. A program that wants them must
        // say so with sei(), the same way it would on any other target -- crt0
        // used to force PSR.IE on here, which made it impossible to start up
        // with interrupts off.
        print_crt0();
    }

    // minc-16 is a different ISA, not a mode of the minc-8 back end, so it has
    // its own code generator (mincc/codegen_16.c).
    if (g_m16) {
        m16_generate_top(code, node_count);
    } else {
        generate_top(code, node_count);
    }

    return EXIT_SUCCESS;
}