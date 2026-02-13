#ifndef ROLE_PROMPTS_H
#define ROLE_PROMPTS_H

/**
 * Role-based system prompt loading for worker agents.
 *
 * Workers receive a system prompt tailored to their role (implementation,
 * code_review, testing, etc.). Prompts are loaded from:
 *   1. File override: <app_home>/prompts/<role>.md (highest priority)
 *   2. Built-in default for known roles
 *   3. Generic worker prompt for unknown roles (lowest priority)
 *
 * Follows the runtime file-loading pattern used by prompt_loader.c
 * (loads AGENTS.md from CWD) and config.c (loads JSON from app_home).
 */

/**
 * Load the system prompt for a worker role.
 *
 * Checks <app_home>/prompts/<role>.md first. If no file exists, returns
 * the built-in default for known roles or a generic worker prompt for
 * unknown roles.
 *
 * @param role  Role name (e.g., "implementation", "code_review", "testing").
 *              NULL or empty string returns the generic worker prompt.
 * @return Newly allocated prompt string (caller must free), or NULL on allocation failure
 */
char *role_prompt_load(const char *role);

/**
 * Get the built-in default prompt for a role without checking file overrides.
 *
 * @param role  Role name. NULL or unknown roles return the generic worker prompt.
 * @return Static string (do NOT free). Never NULL.
 */
const char *role_prompt_builtin(const char *role);

#endif /* ROLE_PROMPTS_H */
