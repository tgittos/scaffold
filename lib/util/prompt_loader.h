#ifndef PROMPT_LOADER_H
#define PROMPT_LOADER_H

/**
 * Load system prompt combining core system prompt with PROMPT.md content
 *
 * Creates a complete system prompt by combining a hard-coded core system prompt
 * with optional user instructions from PROMPT.md file. If PROMPT.md is not found
 * or cannot be read, only the core system prompt is returned.
 *
 * @param prompt_content Pointer to store the loaded prompt content (caller must free)
 * @return 0 on success, -1 on failure (memory allocation error)
 */
int load_system_prompt(char **prompt_content, const char *tools_description);

/**
 * Free memory allocated for prompt content
 *
 * @param prompt_content Pointer to prompt content to free
 */
void cleanup_system_prompt(char **prompt_content);

/**
 * Generate a markdown table of model tiers from config.
 *
 * @return Allocated string (caller must free), or NULL on failure
 */
char* generate_model_tier_table(void);

#endif // PROMPT_LOADER_H
