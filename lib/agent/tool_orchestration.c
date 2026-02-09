#include "tool_orchestration.h"
#include "../policy/protected_files.h"
#include "../policy/tool_args.h"
#include "../policy/verified_file_context.h"
#include "../policy/pattern_generator.h"
#include "../util/debug_output.h"
#include <stdlib.h>
#include <string.h>

static int is_file_write_tool(const char *tool_name) {
    if (tool_name == NULL) return 0;
    return (strcmp(tool_name, "write_file") == 0 ||
            strcmp(tool_name, "append_file") == 0 ||
            strcmp(tool_name, "apply_delta") == 0);
}

static int is_file_tool(const char *tool_name) {
    if (tool_name == NULL) return 0;
    return (strcmp(tool_name, "write_file") == 0 ||
            strcmp(tool_name, "append_file") == 0 ||
            strcmp(tool_name, "apply_delta") == 0 ||
            strcmp(tool_name, "read_file") == 0);
}

int tool_orchestration_init(ToolOrchestrationContext* ctx,
                            ApprovalGateConfig* gate_config) {
    if (ctx == NULL) return -1;
    ctx->gate_config = gate_config;
    ctx->subagent_spawned = 0;
    if (StringArray_init(&ctx->executed_tracker, free) != 0) {
        return -1;
    }
    return 0;
}

void tool_orchestration_cleanup(ToolOrchestrationContext* ctx) {
    if (ctx == NULL) return;
    StringArray_destroy(&ctx->executed_tracker);
    ctx->gate_config = NULL;
    ctx->subagent_spawned = 0;
}

int tool_orchestration_check_approval(ToolOrchestrationContext* ctx,
                                      const ToolCall* tool_call,
                                      ToolResult* result,
                                      ApprovedPath* out_path) {
    if (ctx == NULL || tool_call == NULL || result == NULL) {
        return 0;
    }

    if (is_file_write_tool(tool_call->name)) {
        char *path = tool_args_get_path(tool_call);
        if (path != NULL) {
            if (is_protected_file(path)) {
                result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
                result->result = format_protected_file_error(path);
                result->success = 0;
                free(path);
                return -1;
            }
            free(path);
        }
    }

    if (ctx->gate_config == NULL || !ctx->gate_config->enabled) {
        return 0;
    }

    ApprovedPath approved_path;
    init_approved_path(&approved_path);

    ApprovalResult approval = check_approval_gate(ctx->gate_config,
                                                   tool_call,
                                                   &approved_path);

    switch (approval) {
        case APPROVAL_ALLOWED_ALWAYS: {
            GeneratedPattern gen_pattern = {0};
            if (generate_allowlist_pattern(tool_call, &gen_pattern) == 0) {
                apply_generated_pattern(ctx->gate_config, tool_call->name, &gen_pattern);
                free_generated_pattern(&gen_pattern);
            }
        }
        /* fallthrough */
        case APPROVAL_ALLOWED:
            if (approved_path.resolved_path != NULL && is_file_tool(tool_call->name)) {
                if (out_path != NULL) {
                    /* Move ownership: shallow copy transfers all heap pointers.
                     * Caller is responsible for calling free_approved_path(). */
                    *out_path = approved_path;
                    return 0;
                }
                if (verified_file_context_set(&approved_path) != 0) {
                    VerifyResult verify = verify_approved_path(&approved_path);
                    if (verify != VERIFY_OK) {
                        result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
                        result->result = format_verify_error(verify, approved_path.resolved_path);
                        result->success = 0;
                        free_approved_path(&approved_path);
                        return -1;
                    }
                }
            }
            free_approved_path(&approved_path);
            return 0;

        case APPROVAL_DENIED:
            track_denial(ctx->gate_config, tool_call);
            result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
            result->result = format_denial_error(tool_call);
            result->success = 0;
            free_approved_path(&approved_path);
            return -1;

        case APPROVAL_RATE_LIMITED:
            result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
            result->result = format_rate_limit_error(ctx->gate_config, tool_call);
            result->success = 0;
            free_approved_path(&approved_path);
            return -1;

        case APPROVAL_NON_INTERACTIVE_DENIED:
            result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
            result->result = format_non_interactive_error(tool_call);
            result->success = 0;
            free_approved_path(&approved_path);
            return -1;

        case APPROVAL_ABORTED:
            free_approved_path(&approved_path);
            return -2;
    }

    debug_printf("Warning: Unhandled approval result %d, defaulting to allow\n", approval);
    free_approved_path(&approved_path);
    return 0;
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
