#ifndef MINCC_PARSE_H
#define MINCC_PARSE_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errorhandle.h"
#include "nodes.h"

extern Token *token;

// Tokenizer functions
// Create a new token and link it to the current token
Token *new_token(TokenType type, Token *current, const char *str, unsigned long size, long val, char *loc);

// Tokenize the input string and return the head of the token list
Token *tokenize(const char *p);

// Token consumption functions

// Consume a token if it matches the expected string
// Return true if matched, false otherwise
// If reached EOF, return false
bool consume_la(const char *op, char **loc);
// Consume a token if it matches the expected string
// Return true if matched, false otherwise
// If reached EOF, throw an error
bool consume(const char *op, char **loc);
// Consume tokens if they match the valid type.
// Returns Type_t* if mached, otherwise returns NULL.
// If reached EOF, return NULL.
Type_t *check_type(char **loc);
// Consume a token if it matches the expected string
// Otherwise, throw an error
void expect(const char *op, char **loc);
// Expect a number token and return its value
// Otherwise, throw an error
long expect_number(char **loc);
// Expect an identifier token and return its string
// If reached EOF, throw an error
// Otherwise, throw NULL
char *expect_ident(char **loc);
// Expect a string literal token and return its escape-decoded contents
// Otherwise, throw an error
char *expect_string(char **loc);
bool is_number_token();
bool is_string_token();
bool at_eof();

#endif // MINCC_PARSE_H