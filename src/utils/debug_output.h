#ifndef DEBUG_OUTPUT_H
#define DEBUG_OUTPUT_H

#include <stdio.h>
#include <stdbool.h>

// ANSI color codes
#define DEBUG_COLOR_YELLOW "\033[93m"  // Pale yellow
#define DEBUG_COLOR_RESET  "\033[0m"   // Reset to default

// Global debug flag
extern bool debug_enabled;

// Initialize debug system
void debug_init(bool enable_debug);

// Debug printf - only outputs if debug is enabled, in pale yellow
void debug_printf(const char *format, ...);

// Debug fprintf - only outputs if debug is enabled, in pale yellow
void debug_fprintf(FILE *stream, const char *format, ...);

// Debug print JSON - summarizes large numeric arrays (like embeddings) for readability
// Returns a newly allocated string that must be freed by the caller, or NULL on error
char* debug_summarize_json(const char *json);

// Debug printf for JSON responses - automatically summarizes large arrays
void debug_printf_json(const char *prefix, const char *json);

#endif // DEBUG_OUTPUT_H