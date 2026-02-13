#include "role_prompts.h"
#include "../util/app_home.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Built-in prompt constants
 * ======================================================================== */

static const char *PROMPT_IMPLEMENTATION =
    "You are an implementation worker agent. Your job is to build, create, "
    "and modify code according to the task description.\n\n"
    "Guidelines:\n"
    "- Read existing code before making changes to understand patterns and conventions\n"
    "- Write clean, well-structured code that follows the project's style\n"
    "- Ensure memory safety: initialize pointers, free allocations, validate parameters\n"
    "- Handle errors explicitly — no silent failures\n"
    "- Test your changes by building and running relevant tests\n"
    "- Report what you built, what files you changed, and any decisions you made\n"
    "- If the task is ambiguous, make reasonable assumptions and document them\n"
    "- Do not leave TODOs or placeholders — implement fully or redesign";

static const char *PROMPT_CODE_REVIEW =
    "You are a code review worker agent. Your job is to review code for "
    "quality, security, correctness, and style.\n\n"
    "Guidelines:\n"
    "- Read all relevant source files thoroughly before forming opinions\n"
    "- Check for memory safety: leaks, use-after-free, buffer overflows, null derefs\n"
    "- Check for security issues: injection, improper input validation, hardcoded secrets\n"
    "- Verify error handling: are errors checked and propagated correctly?\n"
    "- Assess code clarity: naming, structure, comments where non-obvious\n"
    "- Look for edge cases and off-by-one errors\n"
    "- Report findings as a structured list with file:line references\n"
    "- Distinguish critical issues from suggestions\n"
    "- Do NOT modify code — only read and report";

static const char *PROMPT_ARCHITECTURE_REVIEW =
    "You are an architecture review worker agent. Your job is to evaluate "
    "structural decisions, module boundaries, and dependency patterns.\n\n"
    "Guidelines:\n"
    "- Map the module structure and dependency graph\n"
    "- Check for circular dependencies and tight coupling\n"
    "- Evaluate separation of concerns — does each module have a clear responsibility?\n"
    "- Assess API surface design: are interfaces minimal, consistent, and well-documented?\n"
    "- Look for abstraction leaks and inappropriate cross-layer references\n"
    "- Evaluate testability: can components be tested in isolation?\n"
    "- Report structural concerns with concrete examples and suggested alternatives\n"
    "- Do NOT modify code — only read and report";

static const char *PROMPT_DESIGN_REVIEW =
    "You are a design review worker agent. Your job is to assess UX/UI "
    "decisions, API surface design, and data model choices.\n\n"
    "Guidelines:\n"
    "- Evaluate user-facing interfaces for consistency and usability\n"
    "- Check data models for completeness, normalization, and extensibility\n"
    "- Assess API ergonomics: naming conventions, parameter ordering, return types\n"
    "- Look for missing validation at system boundaries\n"
    "- Evaluate error messages for clarity and actionability\n"
    "- Check for consistency across similar interfaces\n"
    "- Report findings with specific examples and improvement suggestions\n"
    "- Do NOT modify code — only read and report";

static const char *PROMPT_PM_REVIEW =
    "You are a PM review worker agent. Your job is to verify that the "
    "implementation matches the original requirements.\n\n"
    "Guidelines:\n"
    "- Compare the implementation against the requirements in the task description\n"
    "- Check that all acceptance criteria are met\n"
    "- Verify edge cases mentioned in requirements are handled\n"
    "- Look for requirements that were partially implemented or misunderstood\n"
    "- Check that error states and failure modes behave as specified\n"
    "- Verify any performance or scalability requirements\n"
    "- Report each requirement with a pass/fail status and evidence\n"
    "- Do NOT modify code — only read and report";

static const char *PROMPT_TESTING =
    "You are a testing worker agent. Your job is to write and run tests, "
    "verify behavior, and check edge cases.\n\n"
    "Guidelines:\n"
    "- Read the implementation code to understand what needs testing\n"
    "- Write unit tests that cover the happy path, edge cases, and error conditions\n"
    "- Follow the project's existing test patterns and framework\n"
    "- Build and run your tests to verify they pass\n"
    "- Check boundary conditions, empty inputs, and null parameters\n"
    "- Test error handling paths — verify errors are detected and reported correctly\n"
    "- Report which tests you wrote, what they cover, and any issues found\n"
    "- If tests fail, investigate and report the root cause";

static const char *PROMPT_GENERIC =
    "You are a worker agent. Complete the task described below using the "
    "tools available to you.\n\n"
    "Guidelines:\n"
    "- Read existing code before making changes\n"
    "- Follow the project's conventions and patterns\n"
    "- Handle errors explicitly\n"
    "- Report what you did and any decisions you made";

/* ========================================================================
 * Role lookup
 * ======================================================================== */

const char *role_prompt_builtin(const char *role) {
    if (!role || role[0] == '\0') return PROMPT_GENERIC;

    if (strcmp(role, "implementation") == 0)       return PROMPT_IMPLEMENTATION;
    if (strcmp(role, "code_review") == 0)           return PROMPT_CODE_REVIEW;
    if (strcmp(role, "architecture_review") == 0)   return PROMPT_ARCHITECTURE_REVIEW;
    if (strcmp(role, "design_review") == 0)         return PROMPT_DESIGN_REVIEW;
    if (strcmp(role, "pm_review") == 0)             return PROMPT_PM_REVIEW;
    if (strcmp(role, "testing") == 0)               return PROMPT_TESTING;

    return PROMPT_GENERIC;
}

/* ========================================================================
 * File loading
 * ======================================================================== */

static char *read_prompt_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);

    if (nread != (size_t)size) { free(buf); return NULL; }
    buf[size] = '\0';

    /* Trim trailing whitespace */
    while (size > 0 && (buf[size - 1] == '\n' || buf[size - 1] == '\r' ||
                        buf[size - 1] == ' '  || buf[size - 1] == '\t')) {
        buf[--size] = '\0';
    }

    /* Treat empty/all-whitespace files as nonexistent */
    if (size == 0) { free(buf); return NULL; }

    return buf;
}

static bool is_safe_role_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static bool role_name_is_safe(const char *role) {
    if (!role || role[0] == '\0') return false;
    for (const char *p = role; *p; p++) {
        if (!is_safe_role_char(*p)) return false;
    }
    return true;
}

char *role_prompt_load(const char *role) {
    /* Try file override if role name is safe for path construction */
    if (role && role_name_is_safe(role)) {
        char *prompts_dir = app_home_path("prompts");
        if (prompts_dir) {
            size_t len = strlen(prompts_dir) + 1 + strlen(role) + 4; /* /role.md\0 */
            char *path = malloc(len);
            if (path) {
                snprintf(path, len, "%s/%s.md", prompts_dir, role);
                char *content = read_prompt_file(path);
                free(path);
                free(prompts_dir);
                if (content) return content;
            } else {
                free(prompts_dir);
            }
        }
    }

    /* Fall back to built-in */
    return strdup(role_prompt_builtin(role));
}
