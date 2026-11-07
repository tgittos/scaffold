#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include "log.h"
#include "ansi_codes.h"
#include "debug_output.h"

static LogLevel  g_log_level   = LOG_ERROR;
static uint32_t  g_log_modules = 0;

bool log_http_body_enabled = false;

static pthread_mutex_t log_mutex;
static pthread_once_t  log_mutex_once = PTHREAD_ONCE_INIT;

static void log_mutex_init(void) {
    pthread_mutex_init(&log_mutex, NULL);
}

void log_init(LogLevel level, uint32_t modules) {
    g_log_level   = level;
    g_log_modules = modules;
}

bool log_enabled(LogLevel level, uint32_t module) {
    return (int)level <= (int)g_log_level && (g_log_modules & module);
}

static const char *level_prefix(LogLevel level) {
    switch (level) {
    case LOG_ERROR: return "[ERR]";
    case LOG_WARN:  return "[WRN]";
    case LOG_INFO:  return "[INF]";
    case LOG_DEBUG: return "[DBG]";
    }
    return "[???]";
}

static const char *level_color(LogLevel level) {
    switch (level) {
    case LOG_ERROR: return TERM_BRIGHT_RED;
    case LOG_WARN:  return TERM_BRIGHT_YELLOW;
    case LOG_INFO:  return TERM_BRIGHT_CYAN;
    case LOG_DEBUG: return TERM_GRAY;
    }
    return TERM_RESET;
}

void log_write(LogLevel level, uint32_t module, const char *mod_name,
               const char *format, ...) {
    (void)module;
    pthread_once(&log_mutex_once, log_mutex_init);

    va_list args;
    va_start(args, format);

    const char *color = level_color(level);

    pthread_mutex_lock(&log_mutex);
    fprintf(stderr, "%s%s[%-7s] ", color, level_prefix(level), mod_name);
    vfprintf(stderr, format, args);
    fprintf(stderr, "%s\n", TERM_RESET);
    pthread_mutex_unlock(&log_mutex);

    va_end(args);
}

void log_json(LogLevel level, uint32_t module, const char *mod_name,
              const char *prefix, const char *json) {
    char *summarized = debug_summarize_json(json);
    const char *display = summarized ? summarized : (json ? json : "(null)");
    log_write(level, module, mod_name, "%s%s", prefix ? prefix : "", display);
    free(summarized);
}

int log_parse_level(const char *str) {
    if (str == NULL) return -1;
    if (strcasecmp(str, "error") == 0) return LOG_ERROR;
    if (strcasecmp(str, "warn")  == 0) return LOG_WARN;
    if (strcasecmp(str, "info")  == 0) return LOG_INFO;
    if (strcasecmp(str, "debug") == 0) return LOG_DEBUG;
    return -1;
}

/* Module name -> bit mapping table */
static const struct { const char *name; uint32_t bit; } module_map[] = {
    {"agent",   LOG_MOD_AGENT},
    {"tool",    LOG_MOD_TOOL},
    {"llm",     LOG_MOD_LLM},
    {"http",    LOG_MOD_HTTP},
    {"context", LOG_MOD_CONTEXT},
    {"policy",  LOG_MOD_POLICY},
    {"goap",    LOG_MOD_GOAP},
    {"mcp",     LOG_MOD_MCP},
    {"db",      LOG_MOD_DB},
    {"ipc",     LOG_MOD_IPC},
    {"plugin",  LOG_MOD_PLUGIN},
    {"all",     LOG_MOD_ALL},
    {NULL, 0}
};

uint32_t log_parse_modules(const char *str) {
    if (str == NULL) return 0;

    uint32_t mask = 0;
    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(buf, ",", &saveptr);
    while (tok != NULL) {
        /* trim leading whitespace */
        while (*tok == ' ') tok++;

        bool found = false;
        for (int i = 0; module_map[i].name != NULL; i++) {
            if (strcasecmp(tok, module_map[i].name) == 0) {
                mask |= module_map[i].bit;
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Warning: Unknown log module '%s'\n", tok);
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }
    return mask;
}
