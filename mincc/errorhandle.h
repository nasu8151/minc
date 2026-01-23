#ifndef MINCC_ERRORHANDLE_H
#define MINCC_ERRORHANDLE_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// なかったから是非もないよネ！
// strndup, but raise error() on failure
char *mystrndup(const char *s, size_t n);

extern char *user_input;

// Throw an error message and exit
void error(const char *fmt, ...);
// Throw an error message at a specific location and exit
void error_at(char *loc, const char *fmt, ...);
// Print a warning message
void warn(const char *fmt, ...);
// Print a warning message at a specific location
void warn_at(char *loc, const char *fmt, ...);

#endif // MINCC_ERRORHANDLE_H