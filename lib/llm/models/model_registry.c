#include "../model_capabilities.h"
#include "response_processing.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *pattern;
    int max_context;
    int thinking;
    const char *think_start;
    const char *think_end;
} ModelDataEntry;

static const ModelDataEntry MODEL_DATA[] = {
    { "codex",    400000, 0, NULL,      NULL },
    { "gpt",      128000, 0, NULL,      NULL },
    { "o1",       200000, 0, NULL,      NULL },
    { "o4",       200000, 0, NULL,      NULL },
    { "gemma",    131072, 1, "<think>", "</think>" },
    { "qwen",      32768, 1, "<think>", "</think>" },
    { "deepseek", 128000, 1, "<think>", "</think>" },
    { "claude",   200000, 0, NULL,      NULL },
    { "default",    4096, 0, NULL,      NULL },
};
#define MODEL_DATA_COUNT (sizeof(MODEL_DATA) / sizeof(MODEL_DATA[0]))

int register_all_models(ModelRegistry* registry) {
    if (!registry) return -1;

    for (size_t i = 0; i < MODEL_DATA_COUNT; i++) {
        const ModelDataEntry *e = &MODEL_DATA[i];

        ModelCapabilities *m = calloc(1, sizeof(ModelCapabilities));
        if (!m) return -1;

        m->model_pattern      = e->pattern;
        m->max_context_length  = e->max_context;
        m->supports_thinking_tags = e->thinking;
        m->thinking_start_tag  = e->think_start;
        m->thinking_end_tag    = e->think_end;

        m->process_response = e->thinking
            ? process_thinking_response
            : process_simple_response;

        if (register_model_capabilities(registry, m) != 0) {
            free(m);
            return -1;
        }
    }

    return 0;
}
