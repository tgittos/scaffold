#include "prompt_loader.h"
#include "config.h"
#include <prompt_data.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <limits.h>

static const char *SYSTEM_PROMPT_PART2 = "\n# User Instructions (from AGENTS.md)\n";

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

#define MAX_EXPANDED_FILES 32

// Expands @FILENAME references by inlining file contents wrapped in XML tags.
// Non-recursive: files included this way are not scanned for further @references.
// De-duplicates: each file is expanded only on first occurrence.
static char *expand_file_references(const char *content) {
    if (content == NULL) return NULL;

    StringBuffer buf;
    if (!strbuf_init(&buf, strlen(content) + 1024)) {
        return NULL;
    }

    char *expanded_files[MAX_EXPANDED_FILES];
    int expanded_count = 0;

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
                    for (int i = 0; i < expanded_count; i++) free(expanded_files[i]);
                    strbuf_free(&buf);
                    return NULL;
                }
                memcpy(filename, start, filename_len);
                filename[filename_len] = '\0';

                bool already_expanded = false;
                for (int i = 0; i < expanded_count; i++) {
                    if (strcmp(expanded_files[i], filename) == 0) {
                        already_expanded = true;
                        break;
                    }
                }

                if (already_expanded) {
                    free(filename);
                    p = end;
                    continue;
                }

                char *file_content = read_file_content(filename);

                if (file_content != NULL) {
                    if (!strbuf_append_str(&buf, "<file name=\"") ||
                        !strbuf_append_str(&buf, filename) ||
                        !strbuf_append_str(&buf, "\">\n") ||
                        !strbuf_append_str(&buf, file_content) ||
                        !strbuf_append_str(&buf, "\n</file>")) {
                        free(filename);
                        free(file_content);
                        for (int i = 0; i < expanded_count; i++) free(expanded_files[i]);
                        strbuf_free(&buf);
                        return NULL;
                    }
                    free(file_content);

                    if (expanded_count < MAX_EXPANDED_FILES) {
                        expanded_files[expanded_count++] = filename;
                    } else {
                        free(filename);
                    }

                    p = end;
                    continue;
                } else {
                    free(filename);
                    if (!strbuf_append(&buf, p, 1)) {
                        for (int i = 0; i < expanded_count; i++) free(expanded_files[i]);
                        strbuf_free(&buf);
                        return NULL;
                    }
                    p++;
                    continue;
                }
            } else {
                if (!strbuf_append(&buf, p, 1)) {
                    for (int i = 0; i < expanded_count; i++) free(expanded_files[i]);
                    strbuf_free(&buf);
                    return NULL;
                }
                p++;
                continue;
            }
        } else {
            if (!strbuf_append(&buf, p, 1)) {
                for (int i = 0; i < expanded_count; i++) free(expanded_files[i]);
                strbuf_free(&buf);
                return NULL;
            }
            p++;
        }
    }

    for (int i = 0; i < expanded_count; i++) free(expanded_files[i]);

    char *result = buf.data;
    buf.data = NULL;
    return result;
}

char* generate_model_tier_table(void) {
    const char *simple = config_get_string("model_simple");
    const char *standard = config_get_string("model_standard");
    const char *high = config_get_string("model_high");

    if (!simple) simple = "o4-mini";
    if (!standard) standard = "gpt-5-mini-2025-08-07";
    if (!high) high = "gpt-5.2-2025-12-11";

    const char *format =
        "\n## Model Tiers\n"
        "Select a model tier when spawning subagents via the \"model\" parameter.\n"
        "| Tier | Model |\n"
        "|------|-------|\n"
        "| simple | %s |\n"
        "| standard | %s |\n"
        "| high | %s |\n";

    int size = snprintf(NULL, 0, format, simple, standard, high);
    if (size < 0) return NULL;

    char *result = malloc((size_t)size + 1);
    if (!result) return NULL;

    snprintf(result, (size_t)size + 1, format, simple, standard, high);
    return result;
}

int load_system_prompt(char **prompt_content, const char *tools_description) {
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

    size_t tools_len = tools_description ? strlen(tools_description) : 0;

    char *platform_info = get_platform_info();
    size_t platform_len = platform_info ? strlen(platform_info) : 0;

    char *model_table = generate_model_tier_table();
    size_t model_table_len = model_table ? strlen(model_table) : 0;

    const char *base_prompt = SYSTEM_PROMPT_TEXT;

    size_t part1_len = strlen(base_prompt);
    size_t part2_len = strlen(SYSTEM_PROMPT_PART2);
    size_t user_len = user_prompt ? strlen(user_prompt) : 0;
    size_t total_len = part1_len + platform_len + model_table_len + tools_len + part2_len + user_len + 1;

    char *combined_prompt = malloc(total_len);
    if (combined_prompt == NULL) {
        free(user_prompt);
        free(platform_info);
        free(model_table);
        return -1;
    }

    strcpy(combined_prompt, base_prompt);

    if (platform_info != NULL) {
        strcat(combined_prompt, platform_info);
        free(platform_info);
    }

    if (model_table != NULL) {
        strcat(combined_prompt, model_table);
        free(model_table);
    }

    if (tools_description != NULL) {
        strcat(combined_prompt, tools_description);
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
