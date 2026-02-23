#include "context_enhancement.h"
#include "prompt_mode.h"
#include "../session/rolling_summary.h"
#include "../tools/todo_tool.h"
#include "../tools/memory_tool.h"
#include "../util/context_retriever.h"
#include "../util/json_escape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_RECALL_DEFAULT_K 3
#define MEMORY_ARGS_JSON_OVERHEAD 64  // {"query": "", "k": N}
#define CONTEXT_RETRIEVAL_LIMIT 5

static const char* const MEMORY_SECTION_HEADER = "\n\n# Relevant Memories\n"
                                                "The following memories may be relevant to the current conversation:\n";

static const char* const SUMMARY_SECTION_HEADER =
    "\n\n# Prior Conversation Summary\n"
    "Summary of earlier conversation that has been compacted:\n";

void free_enhanced_prompt_parts(EnhancedPromptParts* parts) {
    if (parts == NULL) return;
    free(parts->base_prompt);
    free(parts->dynamic_context);
    parts->base_prompt = NULL;
    parts->dynamic_context = NULL;
}

static char* retrieve_relevant_memories(const char* query) {
    if (query == NULL || strlen(query) == 0) return NULL;

    ToolCall memory_call = {
        .id = "internal_memory_recall",
        .name = "recall_memories",
        .arguments = NULL
    };

    char* escaped_query = json_escape_string(query);
    if (escaped_query == NULL) return NULL;

    size_t args_len = strlen(escaped_query) + MEMORY_ARGS_JSON_OVERHEAD;
    memory_call.arguments = malloc(args_len);
    if (memory_call.arguments == NULL) {
        free(escaped_query);
        return NULL;
    }

    snprintf(memory_call.arguments, args_len, "{\"query\": \"%s\", \"k\": %d}", escaped_query, MEMORY_RECALL_DEFAULT_K);
    free(escaped_query);

    ToolResult result = {0};
    int exec_result = execute_recall_memories_tool_call(&memory_call, &result);
    free(memory_call.arguments);

    if (exec_result != 0 || !result.success) {
        if (result.result) free(result.result);
        if (result.tool_call_id) free(result.tool_call_id);
        return NULL;
    }

    char* memories = result.result ? strdup(result.result) : NULL;
    free(result.result);
    free(result.tool_call_id);

    return memories;
}

static int todo_list_is_empty(const char* json) {
    return json == NULL || strcmp(json, "{\"todos\":[]}") == 0;
}

static char* build_dynamic_context(const AgentSession* session) {
    if (session == NULL) return NULL;

    char* todo_json = todo_serialize_json((TodoList*)&session->todo_list);
    if (todo_list_is_empty(todo_json)) {
        free(todo_json);
        todo_json = NULL;
    }

    const char* todo_section = "\n\n# Your Internal Todo List State\n"
                              "You have access to an internal todo list system for your own task management. "
                              "This is YOUR todo list for breaking down and tracking your work. "
                              "Your current internal todo list state is:\n\n";

    const char* todo_instructions = "\n\nTODO SYSTEM USAGE:\n"
                                   "- Use TodoWrite to track tasks when the user requests task tracking\n"
                                   "- Update task status (in_progress, completed) as you work on them\n"
                                   "- Only execute tasks if the user explicitly asks you to do so\n"
                                   "- Creating a todo list does NOT mean you should start implementing the tasks\n"
                                   "- Follow the user's actual request, not the existence of todos";

    static const char* const MODE_SECTION_HEADER = "\n\n# Active Mode Instructions\n";
    const char* mode_text = prompt_mode_get_text(session->current_mode);

    if (todo_json == NULL && mode_text == NULL) {
        return NULL;
    }

    size_t total_len = 1;
    if (todo_json != NULL) {
        total_len += strlen(todo_section) + strlen(todo_json) + strlen(todo_instructions);
    }
    if (mode_text != NULL) {
        total_len += strlen(MODE_SECTION_HEADER) + strlen(mode_text);
    }

    char* dynamic = malloc(total_len);
    if (dynamic == NULL) {
        free(todo_json);
        return NULL;
    }

    dynamic[0] = '\0';

    if (todo_json != NULL) {
        strcat(dynamic, todo_section);
        strcat(dynamic, todo_json);
        strcat(dynamic, todo_instructions);
    }

    if (mode_text != NULL) {
        strcat(dynamic, MODE_SECTION_HEADER);
        strcat(dynamic, mode_text);
    }

    free(todo_json);
    return dynamic;
}

int build_enhanced_prompt_parts(const AgentSession* session,
                                const char* user_message,
                                EnhancedPromptParts* out) {
    if (session == NULL || out == NULL) return -1;

    out->base_prompt = NULL;
    out->dynamic_context = NULL;

    const char* base = session->session_data.config.system_prompt;
    out->base_prompt = strdup(base ? base : "");
    if (out->base_prompt == NULL) return -1;

    char* dynamic = build_dynamic_context(session);

    if (user_message == NULL || strlen(user_message) == 0) {
        out->dynamic_context = dynamic;
        return 0;
    }

    char* memories = retrieve_relevant_memories(user_message);
    context_result_t* context = retrieve_relevant_context(user_message, CONTEXT_RETRIEVAL_LIMIT);
    char* formatted_context = NULL;
    if (context && !context->error && context->items.count > 0) {
        formatted_context = format_context_for_prompt(context);
    }

    const RollingSummary* summary = &session->session_data.rolling_summary;
    int has_summary = summary->summary_text != NULL && strlen(summary->summary_text) > 0;

    if (memories == NULL && formatted_context == NULL && !has_summary) {
        free_context_result(context);
        out->dynamic_context = dynamic;
        return 0;
    }

    size_t new_len = 1;
    if (dynamic != NULL) new_len += strlen(dynamic);
    if (memories != NULL) new_len += strlen(MEMORY_SECTION_HEADER) + strlen(memories) + 2;
    if (formatted_context != NULL) new_len += strlen(formatted_context) + 2;
    if (has_summary) new_len += strlen(SUMMARY_SECTION_HEADER) + strlen(summary->summary_text) + 2;

    char* final_dynamic = malloc(new_len);
    if (final_dynamic == NULL) {
        free(memories);
        free(formatted_context);
        free_context_result(context);
        out->dynamic_context = dynamic;
        return 0;
    }

    final_dynamic[0] = '\0';
    if (dynamic != NULL) {
        strcat(final_dynamic, dynamic);
    }

    if (has_summary) {
        strcat(final_dynamic, SUMMARY_SECTION_HEADER);
        strcat(final_dynamic, summary->summary_text);
        strcat(final_dynamic, "\n");
    }

    if (memories != NULL) {
        strcat(final_dynamic, MEMORY_SECTION_HEADER);
        strcat(final_dynamic, memories);
        strcat(final_dynamic, "\n");
    }

    if (formatted_context != NULL) {
        strcat(final_dynamic, formatted_context);
    }

    free(dynamic);
    free(memories);
    free(formatted_context);
    free_context_result(context);

    out->dynamic_context = final_dynamic;
    return 0;
}
