#ifndef DEBUG_OUTPUT_H
#define DEBUG_OUTPUT_H

#include <stdio.h>
#include <stdbool.h>

#define DEBUG_COLOR_YELLOW "\033[93m"
#define DEBUG_COLOR_RESET  "\033[0m"

extern bool debug_enabled;

void debug_init(bool enable_debug);
void debug_printf(const char *format, ...);
void debug_fprintf(FILE *stream, const char *format, ...);

// Summarizes large numeric arrays (e.g. embeddings) for readable debug output.
// Caller must free the returned string.
char* debug_summarize_json(const char *json);

void debug_printf_json(const char *prefix, const char *json);

#endif // DEBUG_OUTPUT_H