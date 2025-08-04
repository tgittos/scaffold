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

#endif // DEBUG_OUTPUT_H