#include "todo_display.h"
#include "../utils/terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TodoDisplayConfig g_display_config = {
    .enabled = true,
    .show_completed = false,
    .compact_mode = true,
    .max_display_items = 5
};

static const char* get_status_symbol(TodoStatus status) {
    switch (status) {
        case TODO_STATUS_PENDING:     return "⏳";
        case TODO_STATUS_IN_PROGRESS: return "🔄";
        case TODO_STATUS_COMPLETED:   return "✅";
        default:                      return "❓";
    }
}

static const char* get_priority_color(TodoPriority priority) {
    switch (priority) {
        case TODO_PRIORITY_HIGH:   return TERM_BRIGHT_RED;
        case TODO_PRIORITY_MEDIUM: return TERM_BRIGHT_YELLOW;
        case TODO_PRIORITY_LOW:    return TERM_BRIGHT_GREEN;
        default:                   return TERM_RESET;
    }
}

int todo_display_init(const TodoDisplayConfig* config) {
    if (config == NULL) return -1;

    g_display_config = *config;
    return 0;
}

void todo_display_print_compact(const TodoList* todo_list) {
    if (!g_display_config.enabled || todo_list == NULL || todo_list->count == 0) {
        return;
    }
    
    int active_count = 0;
    int completed_count = 0;
    
    for (size_t i = 0; i < todo_list->count; i++) {
        if (todo_list->data[i].status == TODO_STATUS_COMPLETED) {
            completed_count++;
        } else {
            active_count++;
        }
    }
    
    if (active_count == 0 && !g_display_config.show_completed) {
        return; // Nothing to show
    }
    
    fprintf(stderr, TERM_DIM TERM_GRAY "[Tasks: %d active", active_count);
    if (completed_count > 0) {
        fprintf(stderr, ", %d completed", completed_count);
    }
    fprintf(stderr, "]\n" TERM_RESET);
    
    int displayed = 0;
    int max_items = (g_display_config.max_display_items > 0) ? 
                    g_display_config.max_display_items : (int)todo_list->count;
    
    for (size_t i = 0; i < todo_list->count && displayed < max_items; i++) {
        const Todo* todo = &todo_list->data[i];
        
        if (todo->status == TODO_STATUS_COMPLETED && !g_display_config.show_completed) {
            continue;
        }
        
        // Prioritize in_progress over pending when space is limited
        if (displayed >= max_items - 1 && todo->status == TODO_STATUS_PENDING) {
            bool has_in_progress = false;
            for (size_t j = i + 1; j < todo_list->count; j++) {
                if (todo_list->data[j].status == TODO_STATUS_IN_PROGRESS) {
                    has_in_progress = true;
                    break;
                }
            }
            if (has_in_progress) continue;
        }
        
        const char* status_symbol = get_status_symbol(todo->status);
        const char* priority_color = get_priority_color(todo->priority);
        
        char truncated_content[80];
        strncpy(truncated_content, todo->content, sizeof(truncated_content) - 1);
        truncated_content[sizeof(truncated_content) - 1] = '\0';
        
        if (strlen(todo->content) > sizeof(truncated_content) - 4) {
            strcpy(truncated_content + sizeof(truncated_content) - 4, "...");
        }
        
        fprintf(stderr, TERM_DIM TERM_GRAY "  %s %s%s" TERM_RESET TERM_DIM TERM_GRAY "\n",
                status_symbol, priority_color, truncated_content);
        
        displayed++;
    }
    
    int remaining = (int)todo_list->count - displayed;
    if (remaining > 0) {
        fprintf(stderr, TERM_DIM TERM_GRAY "  ... and %d more\n" TERM_RESET, remaining);
    }
    
    fprintf(stderr, TERM_RESET);
    fflush(stderr);
}

void todo_display_update(const TodoList* todo_list) {
    todo_display_print_compact(todo_list);
}

void todo_display_cleanup(void) {
    // No dynamic resources to clean up currently
    g_display_config.enabled = false;
}