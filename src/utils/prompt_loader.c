#include "prompt_loader.h"
#include "../tools/python_tool_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Core system prompt PART 1 - up to where dynamic tools list goes
static const char* SYSTEM_PROMPT_PART1 =
    "You are an advanced AI programming agent with access to powerful tools. Use them thoughtfully to maximize user value.\n"
    "\n# Adaptive Behavior Framework\n"
    "Before acting, assess the request complexity and user context:\n"
    "\n## For SIMPLE requests (1-2 actions) or conversations:\n"
    "- Execute directly without formal todo tracking\n"
    "- Use minimal necessary tools\n"
    "- Provide focused, concise responses\n"
    "\n## For COMPLEX requests (3+ distinct actions or multi-file changes):\n"
    "- Break down into logical steps using TodoWrite\n"
    "- Execute systematically with progress tracking\n"
    "- Provide comprehensive implementation\n"
    "\n## Context Sensitivity:\n"
    "- Check git status, recent files for user familiarity\n"
    "- Adapt verbosity to apparent user expertise\n"
    "- Distinguish between exploratory vs. actionable requests\n"
    "\n## Tool Usage Guidelines:\n"
    "- Use tools when they add clear value to the response\n"
    "- Prefer direct answers for known information\n"
    "- Ask for clarification only when genuinely ambiguous\n"
    "\n## Todo Guidelines:\n"
    "- Do not create todos for simple requests or conversations\n"
    "- Do not create todos for requests that can be completed in a single action\n"
    "- Do not create todos for requests that are not actionable\n"
    "\n## Code Exploration Guidelines:\n"
    "When asked to find where something is defined or to understand code:\n"
    "- Search for actual definitions (function signatures, variable declarations), not just mentions in comments\n"
    "- When you find a promising file, READ it to confirm the definition and understand the implementation\n"
    "- Follow code paths: if a function uses a variable, trace where that variable is defined\n"
    "- Be thorough: don't stop at the first match - verify it's the actual definition, not a reference\n"
    "- For functions: look for the implementation body, not just declarations or calls\n"
    "- For variables: find where they are initialized or assigned, not just where they are used\n"
    "\n## Memory Management:\n"
    "You have access to a long-term memory system. Use it wisely:\n"
    "\n### When to Remember (use the 'remember' tool):\n"
    "- User corrections or preferences about how you should behave\n"
    "- Important facts, context, or project-specific knowledge\n"
    "- User instructions that should apply to future sessions\n"
    "- Key information from web fetches that may be referenced later\n"
    "- Project-specific terminology, naming conventions, or patterns\n"
    "\n### What NOT to Remember:\n"
    "- Trivial or transient information\n"
    "- Code that's already in files\n"
    "- Information that changes frequently\n"
    "- Personal or sensitive data (unless explicitly asked)\n"
    "\n### Memory Types:\n"
    "- 'correction': When user corrects your behavior\n"
    "- 'preference': User preferences and settings\n"
    "- 'fact': Important facts or context\n"
    "- 'instruction': Standing instructions for future\n"
    "- 'web_content': Key information from web sources\n"
    "\n### Automatic Memory Recall:\n"
    "- You have access to a semantic memory system that automatically retrieves relevant information from past conversations\n"
    "- When you receive context that references past interactions, this information has been automatically retrieved and injected into your prompt\n"
    "- Check the 'Relevant Memories' section in your context for retrieved information\n"
    "- When using recalled information, you can acknowledge it with phrases like 'Based on our previous discussions...' or 'I recall from our earlier conversation...'\n"
    "- You DO have continuity across sessions through this automatic memory system, even though each session starts fresh from your perspective\n"
    "\n## Python Tool Files:\n"
    "Your external system tools are implemented as Python files in ~/.local/ralph/tools/. "
    "These files are loaded into the persistent Python REPL at startup, making their "
    "functions available in global scope.\n\n"
    "**IMPORTANT**: You may suggest modifications or additions to these tool files, "
    "but you MUST get explicit user confirmation before making any changes. "
    "Never modify tool files without the user's approval.\n\n";

// Core system prompt PART 2 - after the dynamic tools list
static const char* SYSTEM_PROMPT_PART2 =
    "\nThese tools can be extended or modified by editing the Python files directly.\n"
    "\nFollowing describes how the user wants you to behave. Follow these instructions within the above framework.\n"
    "User customization:\n\n";


int load_system_prompt(char **prompt_content) {
    if (prompt_content == NULL) {
        return -1;
    }

    *prompt_content = NULL;

    char *user_prompt = NULL;
    FILE *file = fopen("AGENTS.md", "r");

    if (file != NULL) {
        // Get file size
        if (fseek(file, 0, SEEK_END) == 0) {
            long file_size = ftell(file);
            if (file_size != -1 && fseek(file, 0, SEEK_SET) == 0) {
                // Allocate buffer for file content (+1 for null terminator)
                char *buffer = malloc((size_t)file_size + 1);
                if (buffer != NULL) {
                    // Read file content
                    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
                    if (bytes_read == (size_t)file_size) {
                        // Null-terminate the string
                        buffer[file_size] = '\0';

                        // Remove trailing newlines and whitespace
                        while (file_size > 0 && (buffer[file_size - 1] == '\n' ||
                                                buffer[file_size - 1] == '\r' ||
                                                buffer[file_size - 1] == ' ' ||
                                                buffer[file_size - 1] == '\t')) {
                            buffer[file_size - 1] = '\0';
                            file_size--;
                        }

                        user_prompt = buffer;
                    } else {
                        free(buffer);
                    }
                }
            }
        }
        fclose(file);
    }

    // Get dynamic list of loaded Python tools
    char *tools_desc = python_get_loaded_tools_description();
    size_t tools_len = tools_desc ? strlen(tools_desc) : 0;

    // Calculate total size needed for combined prompt
    size_t part1_len = strlen(SYSTEM_PROMPT_PART1);
    size_t part2_len = strlen(SYSTEM_PROMPT_PART2);
    size_t user_len = user_prompt ? strlen(user_prompt) : 0;
    size_t total_len = part1_len + tools_len + part2_len + user_len + 1;

    // Allocate buffer for combined prompt
    char *combined_prompt = malloc(total_len);
    if (combined_prompt == NULL) {
        free(user_prompt);
        free(tools_desc);
        return -1;
    }

    // Build the combined prompt: PART1 + tools + PART2 + user
    strcpy(combined_prompt, SYSTEM_PROMPT_PART1);

    if (tools_desc != NULL) {
        strcat(combined_prompt, tools_desc);
        free(tools_desc);
    }

    strcat(combined_prompt, SYSTEM_PROMPT_PART2);

    if (user_prompt != NULL) {
        strcat(combined_prompt, user_prompt);
        free(user_prompt);
    }

    *prompt_content = combined_prompt;
    return 0;
}

void cleanup_system_prompt(char **prompt_content) {
    if (prompt_content != NULL && *prompt_content != NULL) {
        free(*prompt_content);
        *prompt_content = NULL;
    }
}
