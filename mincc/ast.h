#ifndef MINCC_AST_H
#define MINCC_AST_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "parse.h"
#include "errorhandle.h"
#include "nodes.h"
#include "codegen.h"

/*
Variable list structure
(top)
+ global_vars -> Ident_Name *next
                    -> ...
+ Vars_list *next (local vars)
    + local_vars -> Ident_Name *next
                    -> ...
    + Vars_list *next (block scope vars)
    + block_vars -> Ident_Name *next
                    -> ...
        + ...
*/

/***************************************************************
program     = toplevel*
toplevel    = type [[attr ("," attr)*]]? ident "=" assign ";" | type [[attr ("," attr)*]]? ident "(" ((expr ",")* expr)? ")" stmt  <-- must be a block
stmt        = expr ";"
            | "{" stmt* "}"
            | "return" expr ";"
            | "if" "(" expr ")" stmt ("else" stmt)?
            | "for" "(" expr? ";" expr? ";" expr? ")" stmt
            | "while" "(" expr ")" stmt
            | "break" ";"
            | "asm" "(" string+ ")" ";"   // inline assembly, emitted verbatim
expr        = assign
assign      = equality ("=" assign)?
bitwise_or     = bitwise_xor ("|" bitwise_xor)*
bitwise_xor    = bitwise_and ("^" bitwise_and)*
bitwise_and    = equality ("&" equality)*
equality    = relational ("==" relational | "!=" relational)*
relational  = add ("<" add | "<=" add | ">" add | ">=" add)*
add         = mul ("+" mul | "-" mul)*
mul         = unary ("*" unary)*
unary       = ("+" | "-" | "~")? primary
            | ("*" | "&") unary
primary     = num | "(" expr ")" | ident | builtin
type        = "uint8_t" | "void" | "int" | "char" "*"*  // Currently uint8_t, int and char mean the same (1 byte int) type.
ident       = type? ("[[" attr "]]")? ident_name | ident_name "(" ((expr ",")* expr)? ")"
attr        = "address" "=" num | "isr" ("=" num)?  // isr=N (N: 0-3) auto-places the function at
                                                     // hardware IRQ vector N; bare isr compiles a
                                                     // correctly-shaped handler without placement.
builtin     = ("sei" | "cli") "(" ")"   // set/clear PSR.IE; expands to an ND_ASM node.
                                         // Only recognized when the name is otherwise undeclared,
                                         // so a user's own `sei`/`cli` still shadows the builtin.
string      = '"' (escape | [^"\\\n])* '"'    // adjacent literals concatenate, as in C
escape      = "\\" ["ntr\\\"']                // \0 is deliberately unsupported
****************************************************************/

// Syntax tree parsing functions
long program();
extern Node code[256];
Node *toplevel(char *l);
Node *close_brace(char *l);
Node *decr(char *l);
Node *stmt(char *l);
Node *assign(char *l);
Node *bitwise_or(char *l);
Node *bitwise_xor(char *l);
Node *bitwise_and(char *l);
Node *and(char *l);
Node *or(char *l);
Node *equality(char *l);
Node *relational(char *l);
Node *expr(char *l);
Node *add(char *l);
Node *mul(char *l);
Node *primary(char *l);
Node *unary(char *l);
Node *ident(char *l);

void new_scope();
long end_scope();

#endif // MINCC_AST_H