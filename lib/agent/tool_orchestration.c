#include "tool_orchestration.h"
#include <stdlib.h>
#include <string.h>

int tool_orchestration_init(ToolOrchestrationContext* ctx) {
    if (ctx == NULL) return -1;
    ctx->subagent_spawned = 0;
    if (StringArray_init(&ctx->executed_tracker, free) != 0) {
        return -1;
    }
    return 0;
}

void tool_orchestration_cleanup(ToolOrchestrationContext* ctx) {
    if (ctx == NULL) return;
    StringArray_destroy(&ctx->executed_tracker);
    ctx->subagent_spawned = 0;
}

int tool_orchestration_is_duplicate(ToolOrchestrationContext* ctx,
                                    const char* tool_call_id) {
    if (ctx == NULL || tool_call_id == NULL) return 0;
    for (size_t i = 0; i < ctx->executed_tracker.count; i++) {
        if (strcmp(ctx->executed_tracker.data[i], tool_call_id) == 0) {
            return 1;
        }
    }
    return 0;
}

int tool_orchestration_mark_executed(ToolOrchestrationContext* ctx,
                                     const char* tool_call_id) {
    if (ctx == NULL || tool_call_id == NULL) {
        return -1;
    }

    char* dup = strdup(tool_call_id);
    if (dup == NULL) {
        return -1;
    }

    if (StringArray_push(&ctx->executed_tracker, dup) != 0) {
        free(dup);
        return -1;
    }

    return 0;
}

int tool_orchestration_can_spawn_subagent(ToolOrchestrationContext* ctx,
                                          const char* tool_name) {
    if (ctx == NULL || tool_name == NULL) return 1;
    if (strcmp(tool_name, "subagent") != 0) return 1;

    if (ctx->subagent_spawned) {
        return 0;
    }

    ctx->subagent_spawned = 1;
    return 1;
}

void tool_orchestration_reset_batch(ToolOrchestrationContext* ctx) {
    if (ctx == NULL) return;
    ctx->subagent_spawned = 0;
}
