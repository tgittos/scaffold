#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Log levels (ascending verbosity).
 */
typedef enum {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3
} LogLevel;

/**
 * Module bitmask tags for filtering log output.
 */
typedef enum {
    LOG_MOD_AGENT   = (1 << 0),
    LOG_MOD_TOOL    = (1 << 1),
    LOG_MOD_LLM     = (1 << 2),
    LOG_MOD_HTTP    = (1 << 3),
    LOG_MOD_CONTEXT = (1 << 4),
    LOG_MOD_POLICY  = (1 << 5),
    LOG_MOD_GOAP    = (1 << 6),
    LOG_MOD_MCP     = (1 << 7),
    LOG_MOD_DB      = (1 << 8),
    LOG_MOD_IPC     = (1 << 9),
    LOG_MOD_PLUGIN  = (1 << 10),
    LOG_MOD_ALL     = 0x7FFFFFFF
} LogModule;

/**
 * Initialize the logging system.
 *
 * @param level  Maximum log level to display
 * @param modules  Bitmask of modules to display (LOG_MOD_ALL for everything)
 */
void log_init(LogLevel level, uint32_t modules);

/**
 * Check whether a message at the given level/module would be displayed.
 */
bool log_enabled(LogLevel level, uint32_t module);

/**
 * Write a log message. Thread-safe, color-coded output to stderr.
 * Appends a newline automatically.
 */
void log_write(LogLevel level, uint32_t module, const char *mod_name,
               const char *format, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * Log a JSON payload with summarization of large numeric arrays.
 * Reuses debug_summarize_json() from debug_output.
 */
void log_json(LogLevel level, uint32_t module, const char *mod_name,
              const char *prefix, const char *json);

/**
 * Parse a log level string ("error", "warn", "info", "debug").
 * Returns -1 on invalid input.
 */
int log_parse_level(const char *str);

/**
 * Parse a comma-separated module filter string (e.g. "tool,goap,http").
 * Returns 0 on error, otherwise the bitmask.
 */
uint32_t log_parse_modules(const char *str);

/**
 * Global flag: when true, CURLOPT_VERBOSE is enabled for HTTP bodies.
 * Controlled by --log-http-body flag.
 */
extern bool log_http_body_enabled;

/*
 * Convenience macros.
 *
 * Each .c file should define LOG_MODULE and LOG_MODULE_STR before
 * including this header:
 *
 *   #define LOG_MODULE     LOG_MOD_TOOL
 *   #define LOG_MODULE_STR "tool"
 *   #include "log.h"
 */
#ifdef LOG_MODULE

#define LOG_ERROR(...) \
    do { if (log_enabled(LOG_ERROR, LOG_MODULE)) \
        log_write(LOG_ERROR, LOG_MODULE, LOG_MODULE_STR, __VA_ARGS__); \
    } while (0)

#define LOG_WARN(...) \
    do { if (log_enabled(LOG_WARN, LOG_MODULE)) \
        log_write(LOG_WARN, LOG_MODULE, LOG_MODULE_STR, __VA_ARGS__); \
    } while (0)

#define LOG_INFO(...) \
    do { if (log_enabled(LOG_INFO, LOG_MODULE)) \
        log_write(LOG_INFO, LOG_MODULE, LOG_MODULE_STR, __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG(...) \
    do { if (log_enabled(LOG_DEBUG, LOG_MODULE)) \
        log_write(LOG_DEBUG, LOG_MODULE, LOG_MODULE_STR, __VA_ARGS__); \
    } while (0)

#define LOG_JSON(level, prefix, json) \
    do { if (log_enabled(level, LOG_MODULE)) \
        log_json(level, LOG_MODULE, LOG_MODULE_STR, prefix, json); \
    } while (0)

#endif /* LOG_MODULE */

#endif /* LOG_H */
