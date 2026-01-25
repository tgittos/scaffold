#include "todo_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANSI_RESET "\033[0m"
#define ANSI_GRAY  "\033[90m"
#define ANSI_DIM   "\033[2m"

// Global display configuration
static TodoDisplayConfig g_display_config = {
    .enabled = true,
    .show_completed = false,
    .compact_mode = true,
    .max_display_items = 5
};

// Status symbols for visual representation
static const char* get_status_symbol(TodoStatus status) {
    switch (status) {
        case TODO_STATUS_PENDING:     return "⏳";
        case TODO_STATUS_IN_PROGRESS: return "🔄";
        case TODO_STATUS_COMPLETED:   return "✅";
        default:                      return "❓";
    }
}

// Priority colors (using existing ANSI color constants)
static const char* get_priority_color(TodoPriority priority) {
    switch (priority) {
        case TODO_PRIORITY_HIGH:   return "\033[91m"; // Bright red
        case TODO_PRIORITY_MEDIUM: return "\033[93m"; // Bright yellow
        case TODO_PRIORITY_LOW:    return "\033[92m"; // Bright green
        default:                   return ANSI_RESET;
    }
}

int todo_display_init(const TodoDisplayConfig* config) {
    if (config == NULL) return -1;
    
    g_display_config = *config;
    return 0;
}

void todo_display_set_enabled(bool enabled) {
    g_display_config.enabled = enabled;
}

bool todo_display_is_enabled(void) {
    return g_display_config.enabled;
}

void todo_display_clear(void) {
    // For simplicity, we don't implement terminal cursor manipulation
    // The compact display format minimizes visual clutter instead
}

void todo_display_print_compact(const TodoList* todo_list) {
    if (!g_display_config.enabled || todo_list == NULL || todo_list->count == 0) {
        return;
    }
    
    // Count active todos (pending + in_progress)
    int active_count = 0;
    int completed_count = 0;
    
    for (size_t i = 0; i < todo_list->count; i++) {
        if (todo_list->todos[i].status == TODO_STATUS_COMPLETED) {
            completed_count++;
        } else {
            active_count++;
        }
    }
    
    if (active_count == 0 && !g_display_config.show_completed) {
        return; // Nothing to show
    }
    
    // Print header with summary
    fprintf(stderr, ANSI_DIM ANSI_GRAY "[Tasks: %d active", active_count);
    if (completed_count > 0) {
        fprintf(stderr, ", %d completed", completed_count);
    }
    fprintf(stderr, "]\n" ANSI_RESET);
    
    // Display todos up to max_display_items
    int displayed = 0;
    int max_items = (g_display_config.max_display_items > 0) ? 
                    g_display_config.max_display_items : (int)todo_list->count;
    
    for (size_t i = 0; i < todo_list->count && displayed < max_items; i++) {
        const Todo* todo = &todo_list->todos[i];
        
        // Skip completed todos unless configured to show them
        if (todo->status == TODO_STATUS_COMPLETED && !g_display_config.show_completed) {
            continue;
        }
        
        // Skip pending todos if we have too many items and prioritize in_progress
        if (displayed >= max_items - 1 && todo->status == TODO_STATUS_PENDING) {
            // Check if there are any in_progress items still to show
            bool has_in_progress = false;
            for (size_t j = i + 1; j < todo_list->count; j++) {
                if (todo_list->todos[j].status == TODO_STATUS_IN_PROGRESS) {
                    has_in_progress = true;
                    break;
                }
            }
            if (has_in_progress) continue;
        }
        
        const char* status_symbol = get_status_symbol(todo->status);
        const char* priority_color = get_priority_color(todo->priority);
        
        // Truncate long content for compact display
        char truncated_content[80];
        strncpy(truncated_content, todo->content, sizeof(truncated_content) - 1);
        truncated_content[sizeof(truncated_content) - 1] = '\0';
        
        if (strlen(todo->content) > sizeof(truncated_content) - 4) {
            strcpy(truncated_content + sizeof(truncated_content) - 4, "...");
        }
        
        fprintf(stderr, ANSI_DIM ANSI_GRAY "  %s %s%s" ANSI_RESET ANSI_DIM ANSI_GRAY "\n",
                status_symbol, priority_color, truncated_content);
        
        displayed++;
    }
    
    // Show "and X more..." if there are additional items
    int remaining = (int)todo_list->count - displayed;
    if (remaining > 0) {
        fprintf(stderr, ANSI_DIM ANSI_GRAY "  ... and %d more\n" ANSI_RESET, remaining);
    }
    
    fprintf(stderr, ANSI_RESET);
    fflush(stderr);
}

void todo_display_update(const TodoList* todo_list) {
    todo_display_print_compact(todo_list);
}

void todo_display_cleanup(void) {
    // No dynamic resources to clean up currently
    g_display_config.enabled = false;
}