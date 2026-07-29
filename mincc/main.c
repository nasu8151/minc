#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
#include "ast.h"
#include "codegen.h"
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

int main() {
    // if (argc != 2) {
    //     fprintf(stderr, "Usage : <code>\n");
    //     return EXIT_FAILURE;
    // }
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
        print_default_crt0(); // byte-for-byte identical to the previous unconditional crt0
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
        printf("mvi r2,2\nstm 2,r2\n"); //enable the interrupts (temporary code. PLEASE change later.)
        print_default_crt0();
    }

    generate_top(code, node_count);

    return EXIT_SUCCESS;
}