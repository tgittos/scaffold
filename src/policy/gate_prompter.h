#ifndef GATE_PROMPTER_H
#define GATE_PROMPTER_H

/**
 * Gate Prompter Module
 *
 * Handles all terminal UI for the approval gate system.
 * Encapsulates TTY detection, terminal mode switching, signal handling,
 * and display formatting for approval prompts.
 *
 * This module provides an opaque type that owns terminal state.
 */

#include "../tools/tools_system.h"  /* For ToolCall */

/**
 * Opaque gate prompter type.
 * Manages terminal state and provides approval prompt UI.
 */
typedef struct GatePrompter GatePrompter;

/**
 * User's response to an approval prompt.
 */
typedef enum {
    PROMPT_RESPONSE_ALLOW,           /* User approved the operation */
    PROMPT_RESPONSE_DENY,            /* User denied the operation */
    PROMPT_RESPONSE_ALLOW_ALWAYS,    /* User approved for this session */
    PROMPT_RESPONSE_DETAILS,         /* User wants more details */
    PROMPT_RESPONSE_CANCELLED,       /* Prompt was interrupted (Ctrl+C) */
    PROMPT_RESPONSE_NO_TTY,          /* No TTY available for prompting */
    PROMPT_RESPONSE_INDIVIDUAL       /* User wants to review individual items (batch mode) */
} PromptResponse;

/* PatternConfirmResult is defined in pattern_generator.h */

/**
 * Create a new gate prompter.
 * Checks for TTY availability and initializes terminal state.
 *
 * @return Newly allocated GatePrompter, or NULL if no TTY is available
 */
GatePrompter *gate_prompter_create(void);

/**
 * Destroy a gate prompter and restore terminal state.
 *
 * @param gp Gate prompter to destroy (can be NULL)
 */
void gate_prompter_destroy(GatePrompter *gp);

/**
 * Check if the gate prompter has an interactive terminal.
 *
 * @param gp Gate prompter
 * @return 1 if interactive, 0 if not (or if gp is NULL)
 */
int gate_prompter_is_interactive(const GatePrompter *gp);

/**
 * Read a single keypress from the terminal.
 * Handles Ctrl+C interruption gracefully.
 *
 * @param gp Gate prompter
 * @return Character read, or -1 on error/interrupt
 */
int gate_prompter_read_key(GatePrompter *gp);

/**
 * Read a keypress with timeout.
 *
 * @param gp Gate prompter
 * @param timeout_ms Timeout in milliseconds
 * @param pressed_key Output: the key that was pressed (if any)
 * @return 1 if a key was pressed, 0 if timeout, -1 on error
 */
int gate_prompter_read_key_timeout(GatePrompter *gp, int timeout_ms, char *pressed_key);

/**
 * Display a single tool approval prompt.
 *
 * @param gp Gate prompter
 * @param tool_call The tool call to prompt for
 * @param command Extracted shell command (or NULL if not a shell tool)
 * @param path Extracted file path (or NULL if not applicable)
 */
void gate_prompter_show_single(GatePrompter *gp,
                               const ToolCall *tool_call,
                               const char *command,
                               const char *path);

/**
 * Display tool details (expanded view).
 *
 * @param gp Gate prompter
 * @param tool_call The tool call to show details for
 * @param resolved_path Resolved absolute path (or NULL if not applicable)
 * @param path_exists 1 if the path exists, 0 if new file
 */
void gate_prompter_show_details(GatePrompter *gp,
                                const ToolCall *tool_call,
                                const char *resolved_path,
                                int path_exists);

/**
 * Display batch approval prompt.
 *
 * @param gp Gate prompter
 * @param tool_calls Array of tool calls
 * @param count Number of tool calls
 * @param statuses Array of status characters for each call
 */
void gate_prompter_show_batch(GatePrompter *gp,
                              const ToolCall *tool_calls,
                              int count,
                              const char *statuses);

/**
 * Print a message to the prompter's output.
 *
 * @param gp Gate prompter
 * @param format printf-style format string
 * @param ... Format arguments
 */
void gate_prompter_print(GatePrompter *gp, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Print a newline to the prompter's output.
 *
 * @param gp Gate prompter
 */
void gate_prompter_newline(GatePrompter *gp);

/**
 * Clear the single-tool approval prompt from the terminal.
 * Moves cursor up and clears to end of screen to remove the prompt lines.
 *
 * @param gp Gate prompter (can be NULL, in which case this is a no-op)
 */
void gate_prompter_clear_prompt(GatePrompter *gp);

/**
 * Clear the batch approval prompt from the terminal.
 * Moves cursor up and clears to end of screen to remove the prompt lines.
 *
 * @param gp Gate prompter (can be NULL, in which case this is a no-op)
 * @param count Number of operations that were shown in the batch prompt
 */
void gate_prompter_clear_batch_prompt(GatePrompter *gp, int count);

#endif /* GATE_PROMPTER_H */
