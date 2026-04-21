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

/*
呼び出し規約（現状）
・引数は左から順にr2~に入れる
・r0~r5は必ず呼び出した側が退避、r6~は呼び出された側が使う都度退避
・ASTを解くときもr2をスタック底としてスタックマシンみたいに解く
*/

void generate_top(Node *code, long i);

// Label generation function
char *get_unique_label(bool isloopend);
char *get_break_label();

// node genelator function
int generate(Node *node);
int gen_i8(Node *node);
int gen_i16(Node *node);
void generate_prologue(long arg_count, long local_var_count);
void generate_epilogue(long arg_count);

#endif // MINCC_CODEGEN_H