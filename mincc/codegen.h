#ifndef MINCC_CODEGEN_H
#define MINCC_CODEGEN_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "nodes.h"
#include "errorhandle.h"

void generate_top(Node *code, long i);

// Label generation function
char *get_unique_label(bool isloopend);
char *get_break_label();

// node genelator function
void generate(Node *node);
void generate_prologue(long arg_count, long local_var_count);
void generate_epilogue();

#endif // MINCC_CODEGEN_H