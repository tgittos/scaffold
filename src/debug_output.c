#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include "debug_output.h"

// Global debug flag
bool debug_enabled = false;

void debug_init(bool enable_debug) {
    debug_enabled = enable_debug;
}

void debug_printf(const char *format, ...) {
    if (!debug_enabled) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    // Print in pale yellow
    fprintf(stderr, DEBUG_COLOR_YELLOW);
    vfprintf(stderr, format, args);
    fprintf(stderr, DEBUG_COLOR_RESET);
    
    va_end(args);
}

void debug_fprintf(FILE *stream, const char *format, ...) {
    (void)stream; // Suppress unused parameter warning - we always use stderr
    
    if (!debug_enabled) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    // Always output to stderr regardless of stream parameter, in pale yellow
    fprintf(stderr, DEBUG_COLOR_YELLOW);
    vfprintf(stderr, format, args);
    fprintf(stderr, DEBUG_COLOR_RESET);
    
    va_end(args);
}