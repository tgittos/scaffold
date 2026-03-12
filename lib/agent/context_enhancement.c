#define LOG_MODULE     LOG_MOD_CONTEXT
#define LOG_MODULE_STR "context"
#include "../util/log.h"
#include "context_enhancement.h"
#include "prompt_mode.h"
#include "../session/rolling_summary.h"
#include "../tools/todo_tool.h"
#include "../tools/memory_tool.h"
#include "../util/context_retriever.h"
#include "../util/json_escape.h"
#include <provider_prompts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* get_provider_addendum(const char* model) {
    if (!model) return PROVIDER_PROMPT_LOCAL;
    if (strstr(model, "claude")) return PROVIDER_PROMPT_ANTHROPIC;
    if (strstr(model, "gpt") || strstr(model, "o1-") || strstr(model, "o3") ||
        strstr(model, "o4") || strstr(model, "codex"))
        return PROVIDER_PROMPT_OPENAI;
    return PROVIDER_PROMPT_LOCAL;
}

#define MEMORY_RECALL_DEFAULT_K 3
#define MEMORY_ARGS_JSON_OVERHEAD 64  // {"query": "", "k": N}
#define CONTEXT_RETRIEVAL_LIMIT 5
#define REPO_SNAPSHOT_MAX_SIZE 2048
#define DIR_TREE_MAX_LINES 80

static const char* const MEMORY_SECTION_HEADER = "\n\n# Relevant Memories\n"
                                                "The following memories may be relevant to the current conversation:\n";

static const char* const SUMMARY_SECTION_HEADER =
    "\n\n# Prior Conversation Summary\n"
    "Summary of earlier conversation that has been compacted:\n";

static const char* const SKIP_DIRS[] = {
    ".git", "node_modules", "__pycache__", "build", "dist",
    "vendor", ".venv", "out", ".cache", ".tox", NULL
};

static const char* const KEY_FILES[] = {
    "README.md", "Makefile", "CMakeLists.txt", "package.json",
    "pyproject.toml", "Cargo.toml", "go.mod", "pom.xml", NULL
};

static int should_skip_dir(const char* name) {
    for (int i = 0; SKIP_DIRS[i] != NULL; i++) {
        if (strcmp(name, SKIP_DIRS[i]) == 0) return 1;
    }
    return 0;
}

static void append_safe(char* buf, size_t buf_size, const char* text) {
    size_t cur = strlen(buf);
    size_t add = strlen(text);
    if (cur + add < buf_size) {
        memcpy(buf + cur, text, add + 1);
    }
}

static int popen_read_line(const char* cmd, char* buf, size_t buf_size) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return -1;
    if (fgets(buf, (int)buf_size, fp) == NULL) {
        pclose(fp);
        return -1;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    pclose(fp);
    return 0;
}

static void tree_recurse(const char* path, int depth, int max_depth,
                         char* buf, size_t buf_size, int* line_count, int max_lines) {
    if (depth > max_depth || *line_count >= max_lines) return;

    DIR* dir = opendir(path);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && *line_count < max_lines) {
        if (entry->d_name[0] == '.') continue;
        if (should_skip_dir(entry->d_name)) continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        char line[512];
        int indent = depth * 2;
        if (indent > 20) indent = 20;
        int written = snprintf(line, sizeof(line), "%*s%s/\n", indent, "", entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(line)) continue;

        if (strlen(buf) + strlen(line) < buf_size) {
            strcat(buf, line);
            (*line_count)++;
            tree_recurse(full_path, depth + 1, max_depth, buf, buf_size, line_count, max_lines);
        }
    }
    closedir(dir);
}

char* build_directory_tree(const char* root, int max_depth) {
    if (!root) return NULL;

    char* buf = calloc(1, REPO_SNAPSHOT_MAX_SIZE);
    if (!buf) return NULL;

    int line_count = 0;
    tree_recurse(root, 0, max_depth, buf, REPO_SNAPSHOT_MAX_SIZE, &line_count, DIR_TREE_MAX_LINES);

    if (buf[0] == '\0') {
        free(buf);
        return NULL;
    }
    return buf;
}

char* build_repo_snapshot(void) {
    char toplevel[512];
    if (popen_read_line("git rev-parse --show-toplevel 2>/dev/null", toplevel, sizeof(toplevel)) != 0) {
        return NULL;
    }

    char* buf = calloc(1, REPO_SNAPSHOT_MAX_SIZE);
    if (!buf) return NULL;

    append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "# Repository Context\n");

    /* Current branch */
    char branch[128];
    if (popen_read_line("git branch --show-current 2>/dev/null", branch, sizeof(branch)) == 0 && branch[0]) {
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "Branch: ");
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, branch);
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "\n");
    }

    /* Recent commits */
    FILE* fp = popen("git log --oneline -5 2>/dev/null", "r");
    if (fp) {
        char line[256];
        int first = 1;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (first) {
                append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "Recent commits:\n");
                first = 0;
            }
            append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "  ");
            append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, line);
        }
        pclose(fp);
    }

    /* Modified files */
    fp = popen("git status --porcelain -uno 2>/dev/null", "r");
    if (fp) {
        char line[256];
        char modified_files[512];
        modified_files[0] = '\0';
        int mod_count = 0;
        while (fgets(line, sizeof(line), fp) != NULL && mod_count < 20) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
            const char* fname = (len > 3) ? line + 3 : line;
            if (mod_count > 0 && strlen(modified_files) + 2 < sizeof(modified_files))
                strcat(modified_files, ", ");
            if (strlen(modified_files) + strlen(fname) < sizeof(modified_files) - 2)
                strcat(modified_files, fname);
            mod_count++;
        }
        pclose(fp);
        if (mod_count > 0) {
            char mod_line[600];
            snprintf(mod_line, sizeof(mod_line), "Modified files: %s (%d file%s)\n",
                     modified_files, mod_count, mod_count == 1 ? "" : "s");
            append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, mod_line);
        }
    }

    /* Key files at project root */
    char key_found[512];
    key_found[0] = '\0';
    for (int i = 0; KEY_FILES[i] != NULL; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", toplevel, KEY_FILES[i]);
        if (access(path, F_OK) == 0) {
            if (key_found[0] && strlen(key_found) + 2 < sizeof(key_found))
                strcat(key_found, ", ");
            if (strlen(key_found) + strlen(KEY_FILES[i]) < sizeof(key_found) - 2)
                strcat(key_found, KEY_FILES[i]);
        }
    }
    if (key_found[0]) {
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "Project root: ");
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, key_found);
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "\n");
    }

    /* Project type detection */
    struct { const char* file; const char* type; } type_map[] = {
        {"Makefile", "C (Makefile)"},
        {"CMakeLists.txt", "C/C++ (CMake)"},
        {"package.json", "JavaScript/TypeScript"},
        {"pyproject.toml", "Python"},
        {"Cargo.toml", "Rust"},
        {"go.mod", "Go"},
        {"pom.xml", "Java"},
        {NULL, NULL}
    };
    char project_types[256];
    project_types[0] = '\0';
    for (int i = 0; type_map[i].file != NULL; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", toplevel, type_map[i].file);
        if (access(path, F_OK) == 0) {
            if (project_types[0] && strlen(project_types) + 2 < sizeof(project_types))
                strcat(project_types, ", ");
            if (strlen(project_types) + strlen(type_map[i].type) < sizeof(project_types) - 2)
                strcat(project_types, type_map[i].type);
        }
    }
    if (project_types[0]) {
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "Project type: ");
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, project_types);
        append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, "\n");
    }

    /* Directory tree */
    char* tree = build_directory_tree(toplevel, 2);
    if (tree) {
        size_t cur_len = strlen(buf);
        size_t tree_len = strlen(tree);
        static const char* tree_header = "\n# Directory Structure (depth 2)\n";
        size_t header_len = strlen(tree_header);

        if (cur_len + header_len + tree_len < REPO_SNAPSHOT_MAX_SIZE) {
            append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, tree_header);
            append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, tree);
        } else if (cur_len + header_len + 100 < REPO_SNAPSHOT_MAX_SIZE) {
            /* Truncate tree to fit within budget */
            append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, tree_header);
            size_t avail = REPO_SNAPSHOT_MAX_SIZE - strlen(buf) - 1;
            if (avail > 0 && avail < tree_len) {
                tree[avail] = '\0';
                char* last_nl = strrchr(tree, '\n');
                if (last_nl) *(last_nl + 1) = '\0';
            }
            append_safe(buf, REPO_SNAPSHOT_MAX_SIZE, tree);
        }
        free(tree);
    }

    return buf;
}

void free_enhanced_prompt_parts(EnhancedPromptParts* parts) {
    if (parts == NULL) return;
    free(parts->base_prompt);
    free(parts->dynamic_context);
    parts->base_prompt = NULL;
    parts->dynamic_context = NULL;
}

static char* retrieve_relevant_memories(const char* query) {
    if (query == NULL || strlen(query) == 0) return NULL;

    ToolCall memory_call = {
        .id = "internal_memory_recall",
        .name = "recall_memories",
        .arguments = NULL
    };

    char* escaped_query = json_escape_string(query);
    if (escaped_query == NULL) return NULL;

    size_t args_len = strlen(escaped_query) + MEMORY_ARGS_JSON_OVERHEAD;
    memory_call.arguments = malloc(args_len);
    if (memory_call.arguments == NULL) {
        free(escaped_query);
        return NULL;
    }

    snprintf(memory_call.arguments, args_len, "{\"query\": \"%s\", \"k\": %d}", escaped_query, MEMORY_RECALL_DEFAULT_K);
    free(escaped_query);

    ToolResult result = {0};
    int exec_result = execute_recall_memories_tool_call(&memory_call, &result);
    free(memory_call.arguments);

    if (exec_result != 0 || !result.success) {
        if (result.result) free(result.result);
        if (result.tool_call_id) free(result.tool_call_id);
        return NULL;
    }

    char* memories = result.result ? strdup(result.result) : NULL;
    free(result.result);
    free(result.tool_call_id);

    return memories;
}

static int todo_list_is_empty(const char* json) {
    return json == NULL || strcmp(json, "{\"todos\":[]}") == 0;
}

static char* build_dynamic_context(const AgentSession* session) {
    if (session == NULL) return NULL;

    char* todo_json = todo_serialize_json((TodoList*)&session->todo_list);
    if (todo_list_is_empty(todo_json)) {
        free(todo_json);
        todo_json = NULL;
    }

    const char* todo_section = "\n\n# Your Internal Todo List State\n"
                              "You have access to an internal todo list system for your own task management. "
                              "This is YOUR todo list for breaking down and tracking your work. "
                              "Your current internal todo list state is:\n\n";

    const char* todo_instructions = "\n\nTODO SYSTEM USAGE:\n"
                                   "- Use TodoWrite to break medium/complex tasks into steps and track progress\n"
                                   "- Update task status (in_progress, completed) as you work through them\n"
                                   "- Work through your todo items — they are YOUR execution plan, not a proposal";

    static const char* const MODE_SECTION_HEADER = "\n\n# Active Mode Instructions\n";
    const char* mode_text = prompt_mode_get_text(session->current_mode);

    static const char* const PROVIDER_SECTION_HEADER = "\n\n# Provider Notes\n";
    const char* provider_text = get_provider_addendum(session->session_data.config.model);

    /* Repo context on first turn only */
    char* repo_ctx = NULL;
    if (!session->first_turn_context_injected) {
        repo_ctx = build_repo_snapshot();
    }

    LOG_DEBUG("Dynamic context: todos=%s, mode=%s, provider=%s, repo_context=%s",
              todo_json ? "yes" : "no",
              mode_text ? prompt_mode_name(session->current_mode) : "none",
              session->session_data.config.model ? session->session_data.config.model : "unknown",
              repo_ctx ? "yes" : "no");

    if (todo_json == NULL && mode_text == NULL && provider_text == NULL && repo_ctx == NULL) {
        return NULL;
    }

    size_t total_len = 1;
    if (todo_json != NULL) {
        total_len += strlen(todo_section) + strlen(todo_json) + strlen(todo_instructions);
    }
    if (mode_text != NULL) {
        total_len += strlen(MODE_SECTION_HEADER) + strlen(mode_text);
    }
    if (provider_text != NULL) {
        total_len += strlen(PROVIDER_SECTION_HEADER) + strlen(provider_text);
    }
    if (repo_ctx != NULL) {
        total_len += 2 + strlen(repo_ctx);  /* "\n\n" + repo context */
    }

    char* dynamic = malloc(total_len);
    if (dynamic == NULL) {
        free(todo_json);
        free(repo_ctx);
        return NULL;
    }

    dynamic[0] = '\0';

    if (repo_ctx != NULL) {
        strcat(dynamic, "\n\n");
        strcat(dynamic, repo_ctx);
        free(repo_ctx);
    }

    if (todo_json != NULL) {
        strcat(dynamic, todo_section);
        strcat(dynamic, todo_json);
        strcat(dynamic, todo_instructions);
    }

    if (mode_text != NULL) {
        strcat(dynamic, MODE_SECTION_HEADER);
        strcat(dynamic, mode_text);
    }

    if (provider_text != NULL) {
        strcat(dynamic, PROVIDER_SECTION_HEADER);
        strcat(dynamic, provider_text);
    }

    free(todo_json);
    return dynamic;
}

int build_enhanced_prompt_parts(const AgentSession* session,
                                const char* user_message,
                                EnhancedPromptParts* out) {
    if (session == NULL || out == NULL) return -1;

    out->base_prompt = NULL;
    out->dynamic_context = NULL;

    const char* base = session->session_data.config.system_prompt;
    out->base_prompt = strdup(base ? base : "");
    if (out->base_prompt == NULL) return -1;

    char* dynamic = build_dynamic_context(session);

    /* Mark first turn context as injected */
    if (!session->first_turn_context_injected) {
        ((AgentSession*)session)->first_turn_context_injected = 1;
    }

    if (user_message == NULL || strlen(user_message) == 0) {
        out->dynamic_context = dynamic;
        return 0;
    }

    LOG_DEBUG("Enhanced prompt: base=%zu chars, dynamic=%zu chars",
              strlen(out->base_prompt), dynamic ? strlen(dynamic) : 0);

    char* memories = retrieve_relevant_memories(user_message);
    context_result_t* context = retrieve_relevant_context(user_message, CONTEXT_RETRIEVAL_LIMIT);
    char* formatted_context = NULL;
    if (context && !context->error && context->items.count > 0) {
        formatted_context = format_context_for_prompt(context);
    }

    const RollingSummary* summary = &session->session_data.rolling_summary;
    int has_summary = summary->summary_text != NULL && strlen(summary->summary_text) > 0;

    if (memories == NULL && formatted_context == NULL && !has_summary) {
        free_context_result(context);
        out->dynamic_context = dynamic;
        return 0;
    }

    size_t new_len = 1;
    if (dynamic != NULL) new_len += strlen(dynamic);
    if (memories != NULL) new_len += strlen(MEMORY_SECTION_HEADER) + strlen(memories) + 2;
    if (formatted_context != NULL) new_len += strlen(formatted_context) + 2;
    if (has_summary) new_len += strlen(SUMMARY_SECTION_HEADER) + strlen(summary->summary_text) + 2;

    char* final_dynamic = malloc(new_len);
    if (final_dynamic == NULL) {
        free(memories);
        free(formatted_context);
        free_context_result(context);
        out->dynamic_context = dynamic;
        return 0;
    }

    final_dynamic[0] = '\0';
    if (dynamic != NULL) {
        strcat(final_dynamic, dynamic);
    }

    if (has_summary) {
        strcat(final_dynamic, SUMMARY_SECTION_HEADER);
        strcat(final_dynamic, summary->summary_text);
        strcat(final_dynamic, "\n");
    }

    if (memories != NULL) {
        strcat(final_dynamic, MEMORY_SECTION_HEADER);
        strcat(final_dynamic, memories);
        strcat(final_dynamic, "\n");
    }

    if (formatted_context != NULL) {
        strcat(final_dynamic, formatted_context);
    }

    free(dynamic);
    free(memories);
    free(formatted_context);
    free_context_result(context);

    out->dynamic_context = final_dynamic;
    return 0;
}
