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
toplevel    = type [[attr]]? ident "=" assign ";" | type ident "(" ((expr ",")* expr)? ")" stmt  <-- must be a block
stmt        = expr ";"
            | "{" stmt* "}"
            | "return" expr ";"
            | "if" "(" expr ")" stmt ("else" stmt)?
            | "for" "(" expr? ";" expr? ";" expr? ")" stmt
            | "while" "(" expr ")" stmt
            | "break" ";"
expr       = assign
assign     = equality ("=" assign)?
bitwise_or     = bitwise_xor ("|" bitwise_xor)*
bitwise_xor    = bitwise_and ("^" bitwise_and)*
bitwise_and    = equality ("&" equality)*
equality   = relational ("==" relational | "!=" relational)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
add        = mul ("+" mul | "-" mul)*
mul        = unary ("*" unary)*
unary      = ("+" | "-" | "~")? primary
primary    = num | type? ("[[" attr "]]")? ident | ident "(" ((expr ",")* expr)? ")" | "(" expr ")"
type       = "uint8_t" | "void" | "int" | "char"  // Currently uint8_t, int and char mean the same (1 byte int) type.
attr       = ("address") "=" num
****************************************************************/

// Syntax tree parsing functions
void program();
Node *toplevel(char *l);
Node *stmt(char *l);
Node *assign(char *l);
Node *bitwise_or(char *l);
Node *bitwise_xor(char *l);
Node *bitwise_and(char *l);
Node *equality(char *l);
Node *relational(char *l);
Node *expr(char *l);
Node *add(char *l);
Node *mul(char *l);
Node *primary(char *l);
Node *unary(char *l);

void new_scope();
long end_scope();

#endif // MINCC_AST_H