#ifndef PROMPT_MODE_H
#define PROMPT_MODE_H

typedef enum {
    PROMPT_MODE_DEFAULT = 0,
    PROMPT_MODE_PLAN,
    PROMPT_MODE_EXPLORE,
    PROMPT_MODE_DEBUG,
    PROMPT_MODE_REVIEW,
    PROMPT_MODE_COUNT
} PromptMode;

/**
 * Get the display name of a prompt mode.
 * @return Static string (e.g., "plan"), or "default" for PROMPT_MODE_DEFAULT.
 */
const char* prompt_mode_name(PromptMode mode);

/**
 * Parse a mode name string into a PromptMode enum value.
 * @param name  Mode name (case-insensitive)
 * @param out   Output enum value
 * @return 0 on success, -1 if name is unrecognized
 */
int prompt_mode_from_name(const char* name, PromptMode* out);

/**
 * Get the compiled-in behavioral overlay text for a mode.
 * @return Static string with mode instructions, or NULL for PROMPT_MODE_DEFAULT.
 */
const char* prompt_mode_get_text(PromptMode mode);

/**
 * Get a short human-readable description of a mode.
 * @return Static string describing the mode's purpose.
 */
const char* prompt_mode_description(PromptMode mode);

#endif /* PROMPT_MODE_H */
