#ifndef TODO_DISPLAY_H
#define TODO_DISPLAY_H

#include "todo_manager.h"
#include <stdbool.h>

/**
 * Todo display configuration
 */
typedef struct {
    bool enabled;           // Whether todo display is enabled
    bool show_completed;    // Whether to show completed todos
    bool compact_mode;      // Use compact single-line format
    int max_display_items;  // Maximum number of todos to display (-1 for all)
} TodoDisplayConfig;

/**
 * Initialize todo display system
 * 
 * @param config Display configuration
 * @return 0 on success, -1 on failure
 */
int todo_display_init(const TodoDisplayConfig* config);

/**
 * Update the todo list display 
 * Called whenever the todo list changes to refresh the display
 * 
 * @param todo_list Current todo list state
 */
void todo_display_update(const TodoList* todo_list);

/**
 * Clear the current todo display from the console
 */
void todo_display_clear(void);

/**
 * Enable or disable todo display
 * 
 * @param enabled Whether to enable display
 */
void todo_display_set_enabled(bool enabled);

/**
 * Check if todo display is currently enabled
 * 
 * @return true if enabled, false otherwise
 */
bool todo_display_is_enabled(void);

/**
 * Print todo list in a compact, unobtrusive format
 * This is called directly by the todo tool after updates
 * 
 * @param todo_list Todo list to display
 */
void todo_display_print_compact(const TodoList* todo_list);

/**
 * Cleanup todo display system
 */
void todo_display_cleanup(void);

/**
 * Flush any deferred todo display
 * Called after tool execution group ends to show deferred updates
 */
void todo_display_flush_deferred(void);

#endif // TODO_DISPLAY_H