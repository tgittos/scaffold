#ifndef PROMPT_LOADER_H
#define PROMPT_LOADER_H

/**
 * Load system prompt from PROMPT.md file in current working directory
 * 
 * @param prompt_content Pointer to store the loaded prompt content (caller must free)
 * @return 0 on success, -1 on failure (file not found, read error, memory allocation error)
 */
int load_system_prompt(char **prompt_content);

/**
 * Free memory allocated for prompt content
 * 
 * @param prompt_content Pointer to prompt content to free
 */
void cleanup_system_prompt(char **prompt_content);

#endif // PROMPT_LOADER_H