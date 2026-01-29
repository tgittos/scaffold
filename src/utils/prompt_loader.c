#include "prompt_loader.h"
#include "../tools/python_tool_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <limits.h>

static char *get_platform_info(void) {
    struct utsname uname_info;
    char cwd[PATH_MAX];

    memset(&uname_info, 0, sizeof(uname_info));  // Zero-initialize for valgrind
    memset(cwd, 0, sizeof(cwd));

    const char *arch = "unknown";
    const char *os_name = "unknown";
    const char *cwd_str = ".";

    if (uname(&uname_info) == 0) {
        arch = uname_info.machine;
        os_name = uname_info.sysname;
    }

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        cwd_str = cwd;
    }

    const char *format =
        "\n## Platform Information:\n"
        "- Architecture: %s\n"
        "- Operating System: %s\n"
        "- Working Directory: %s\n";

    int size = snprintf(NULL, 0, format, arch, os_name, cwd_str);
    if (size < 0) {
        return NULL;
    }

    char *result = malloc((size_t)size + 1);
    if (result == NULL) {
        return NULL;
    }

    snprintf(result, (size_t)size + 1, format, arch, os_name, cwd_str);
    return result;
}

static const char* SYSTEM_PROMPT_PART1 =
    "You are an AI programming agent. Use your tools to help users with software tasks.\n"
    "\n# Core Principles\n"
    "- Act immediately on requests. Don't ask 'Should I proceed?' - just do it.\n"
    "- Only ask questions when genuinely ambiguous.\n"
    "- Simple tasks (1-2 actions): execute directly, no todo tracking.\n"
    "- Complex tasks (3+ actions): use TodoWrite for systematic progress.\n"
    "- Adapt verbosity to user expertise.\n"
    "\n# Code Exploration\n"
    "When finding definitions: search for actual implementations, READ files to confirm, "
    "trace variable origins, verify you found the definition not just a reference.\n"
    "\n# Memory System\n"
    "Use 'remember' tool for: user corrections, preferences, standing instructions, "
    "project-specific knowledge. Don't remember: transient info, code already in files.\n"
    "Memory types: correction, preference, fact, instruction, web_content.\n"
    "Relevant memories are auto-retrieved into your context.\n"
    "\n# Inter-Agent Messaging\n"
    "Tools: get_agent_info (discover IDs), send_message, check_messages, "
    "subscribe_channel, publish_channel, check_channel_messages.\n"
    "Pattern: get_agent_info -> send_message to parent_agent_id to report results.\n"
    "\n# Python Tools\n"
    "External tools in ~/.local/ralph/tools/ are loaded into the Python REPL at startup.\n\n";

static const char* SYSTEM_PROMPT_PART2 =
    "\n# User Instructions (from AGENTS.md)\n";


static bool is_valid_filename_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '/';
}

static bool looks_like_file_path(const char *str, size_t len) {
    if (len == 0) return false;

    bool has_dot = false;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '.') {
            has_dot = true;
            break;
        }
    }
    return has_dot;
}

static char *read_file_content(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);
    if (file_size == -1 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *content = malloc((size_t)file_size + 1);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(content, 1, (size_t)file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(content);
        return NULL;
    }

    content[file_size] = '\0';
    return content;
}

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} StringBuffer;

static bool strbuf_init(StringBuffer *buf, size_t initial_capacity) {
    buf->data = malloc(initial_capacity);
    if (buf->data == NULL) return false;
    buf->data[0] = '\0';
    buf->len = 0;
    buf->capacity = initial_capacity;
    return true;
}

static bool strbuf_append(StringBuffer *buf, const char *str, size_t len) {
    if (len == 0) return true;

    size_t new_len = buf->len + len;
    if (new_len + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        while (new_capacity < new_len + 1) {
            new_capacity *= 2;
        }
        char *new_data = realloc(buf->data, new_capacity);
        if (new_data == NULL) return false;
        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->len, str, len);
    buf->len = new_len;
    buf->data[buf->len] = '\0';
    return true;
}

static bool strbuf_append_str(StringBuffer *buf, const char *str) {
    return strbuf_append(buf, str, strlen(str));
}

static void strbuf_free(StringBuffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

// Expands @FILENAME references by inlining file contents wrapped in XML tags.
// Non-recursive: files included this way are not scanned for further @references.
static char *expand_file_references(const char *content) {
    if (content == NULL) return NULL;

    StringBuffer buf;
    if (!strbuf_init(&buf, strlen(content) + 1024)) {
        return NULL;
    }

    const char *p = content;
    while (*p != '\0') {
        if (*p == '@') {
            const char *start = p + 1;
            const char *end = start;

            while (*end != '\0' && is_valid_filename_char(*end)) {
                end++;
            }

            size_t filename_len = (size_t)(end - start);

            if (filename_len > 0 && looks_like_file_path(start, filename_len)) {
                char *filename = malloc(filename_len + 1);
                if (filename == NULL) {
                    strbuf_free(&buf);
                    return NULL;
                }
                memcpy(filename, start, filename_len);
                filename[filename_len] = '\0';

                char *file_content = read_file_content(filename);

                if (file_content != NULL) {
                    if (!strbuf_append_str(&buf, "<file name=\"") ||
                        !strbuf_append_str(&buf, filename) ||
                        !strbuf_append_str(&buf, "\">\n") ||
                        !strbuf_append_str(&buf, file_content) ||
                        !strbuf_append_str(&buf, "\n</file>")) {
                        free(filename);
                        free(file_content);
                        strbuf_free(&buf);
                        return NULL;
                    }
                    free(file_content);
                    free(filename);
                    p = end;  // Skip past the @FILENAME
                    continue;
                } else {
                    free(filename);
                    if (!strbuf_append(&buf, p, 1)) {
                        strbuf_free(&buf);
                        return NULL;
                    }
                    p++;
                    continue;
                }
            } else {
                if (!strbuf_append(&buf, p, 1)) {
                    strbuf_free(&buf);
                    return NULL;
                }
                p++;
                continue;
            }
        } else {
            if (!strbuf_append(&buf, p, 1)) {
                strbuf_free(&buf);
                return NULL;
            }
            p++;
        }
    }

    char *result = buf.data;
    buf.data = NULL;
    return result;
}

int load_system_prompt(char **prompt_content) {
    if (prompt_content == NULL) {
        return -1;
    }

    *prompt_content = NULL;

    char *user_prompt = NULL;
    FILE *file = fopen("AGENTS.md", "r");

    if (file != NULL) {
        if (fseek(file, 0, SEEK_END) == 0) {
            long file_size = ftell(file);
            if (file_size != -1 && fseek(file, 0, SEEK_SET) == 0) {
                char *buffer = malloc((size_t)file_size + 1);
                if (buffer != NULL) {
                    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
                    if (bytes_read == (size_t)file_size) {
                        buffer[file_size] = '\0';
                        while (file_size > 0 && (buffer[file_size - 1] == '\n' ||
                                                buffer[file_size - 1] == '\r' ||
                                                buffer[file_size - 1] == ' ' ||
                                                buffer[file_size - 1] == '\t')) {
                            buffer[file_size - 1] = '\0';
                            file_size--;
                        }

                        char *expanded = expand_file_references(buffer);
                        if (expanded != NULL) {
                            free(buffer);
                            user_prompt = expanded;
                        } else {
                            user_prompt = buffer;
                        }
                    } else {
                        free(buffer);
                    }
                }
            }
        }
        fclose(file);
    }

    char *tools_desc = python_get_loaded_tools_description();
    size_t tools_len = tools_desc ? strlen(tools_desc) : 0;

    char *platform_info = get_platform_info();
    size_t platform_len = platform_info ? strlen(platform_info) : 0;

    size_t part1_len = strlen(SYSTEM_PROMPT_PART1);
    size_t part2_len = strlen(SYSTEM_PROMPT_PART2);
    size_t user_len = user_prompt ? strlen(user_prompt) : 0;
    size_t total_len = part1_len + platform_len + tools_len + part2_len + user_len + 1;

    char *combined_prompt = malloc(total_len);
    if (combined_prompt == NULL) {
        free(user_prompt);
        free(tools_desc);
        free(platform_info);
        return -1;
    }

    strcpy(combined_prompt, SYSTEM_PROMPT_PART1);

    if (platform_info != NULL) {
        strcat(combined_prompt, platform_info);
        free(platform_info);
    }

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
