#include "context_enhancement.h"
#include "ralph.h"
#include "todo_tool.h"
#include "memory_tool.h"
#include "../utils/context_retriever.h"
#include "json_escape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants for memory recall
#define MEMORY_RECALL_DEFAULT_K 3
#define MEMORY_ARGS_JSON_OVERHEAD 64  // Overhead for JSON structure: {"query": "", "k": N}

// Constants for context retrieval
#define CONTEXT_RETRIEVAL_LIMIT 5

// Memory section header for prompts (const pointer to prevent accidental reassignment)
static const char* const MEMORY_SECTION_HEADER = "\n\n# Relevant Memories\n"
                                                "The following memories may be relevant to the current conversation:\n";

// Helper function to retrieve relevant memories based on user message
static char* retrieve_relevant_memories(const char* query) {
    if (query == NULL || strlen(query) == 0) return NULL;

    // Create a tool call to recall memories
    ToolCall memory_call = {
        .id = "internal_memory_recall",
        .name = "recall_memories",
        .arguments = NULL
    };

    // Build arguments JSON
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

    // Execute the recall
    ToolResult result = {0};
    int exec_result = execute_recall_memories_tool_call(&memory_call, &result);
    free(memory_call.arguments);

    if (exec_result != 0 || !result.success) {
        if (result.result) free(result.result);
        if (result.tool_call_id) free(result.tool_call_id);
        return NULL;
    }

    // Extract memories from result
    char* memories = result.result ? strdup(result.result) : NULL;
    free(result.result);
    free(result.tool_call_id);

    return memories;
}

static char* ralph_build_enhanced_system_prompt(const RalphSession* session) {
    if (session == NULL) return NULL;

    const char* base_prompt = session->session_data.config.system_prompt;
    if (base_prompt == NULL) base_prompt = "";

    // Serialize current todo list state
    char* todo_json = todo_serialize_json((TodoList*)&session->todo_list);
    if (todo_json == NULL) {
        // If serialization fails, just return the base prompt
        size_t len = strlen(base_prompt) + 1;
        char* result = malloc(len);
        if (result) strcpy(result, base_prompt);
        return result;
    }

    // Build enhanced prompt with todo context
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

    size_t total_len = strlen(base_prompt) + strlen(todo_section) +
                       strlen(todo_json) + strlen(todo_instructions) + 1;

    char* enhanced_prompt = malloc(total_len);
    if (enhanced_prompt == NULL) {
        free(todo_json);
        return NULL;
    }

    snprintf(enhanced_prompt, total_len, "%s%s%s%s",
             base_prompt, todo_section, todo_json, todo_instructions);

    free(todo_json);
    return enhanced_prompt;
}

char* build_enhanced_prompt_with_context(const RalphSession* session,
                                         const char* user_message) {
    if (session == NULL) return NULL;

    char* enhanced_prompt = ralph_build_enhanced_system_prompt(session);
    if (enhanced_prompt == NULL) return NULL;

    // If no user message, just return the enhanced prompt
    if (user_message == NULL || strlen(user_message) == 0) {
        return enhanced_prompt;
    }

    // Retrieve relevant memories
    char* memories = retrieve_relevant_memories(user_message);

    // Retrieve relevant context from vector database
    context_result_t* context = retrieve_relevant_context(user_message, CONTEXT_RETRIEVAL_LIMIT);
    char* formatted_context = NULL;
    if (context && !context->error && context->items.count > 0) {
        formatted_context = format_context_for_prompt(context);
    }

    // If no memories or context, return enhanced prompt as-is
    if (memories == NULL && formatted_context == NULL) {
        free_context_result(context);
        return enhanced_prompt;
    }

    // Calculate size for combined prompt
    size_t new_len = strlen(enhanced_prompt) + 1;

    if (memories != NULL) {
        new_len += strlen(MEMORY_SECTION_HEADER) + strlen(memories) + 2;
    }

    if (formatted_context != NULL) {
        new_len += strlen(formatted_context) + 2;
    }

    // Allocate and build the final prompt
    char* final_prompt = malloc(new_len);
    if (final_prompt == NULL) {
        // On allocation failure, fall back to enhanced_prompt
        free(memories);
        free(formatted_context);
        free_context_result(context);
        return enhanced_prompt;
    }

    strcpy(final_prompt, enhanced_prompt);

    if (memories != NULL) {
        strcat(final_prompt, MEMORY_SECTION_HEADER);
        strcat(final_prompt, memories);
        strcat(final_prompt, "\n");
    }

    if (formatted_context != NULL) {
        strcat(final_prompt, formatted_context);
    }

    free(enhanced_prompt);
    free(memories);
    free(formatted_context);
    free_context_result(context);

    return final_prompt;
}
