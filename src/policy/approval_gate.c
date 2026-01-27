/**
 * Approval Gates Implementation
 *
 * This module provides the core approval gate logic for requiring user
 * confirmation before executing potentially destructive operations.
 *
 * See SPEC_APPROVAL_GATES.md for the full specification.
 */

#include "approval_gate.h"
#include "gate_prompter.h"
#include "pattern_generator.h"
#include "shell_parser.h"
#include "subagent_approval.h"
#include "tool_args.h"

#include <cJSON.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../tools/python_tool_files.h"
#include "../tools/subagent_tool.h"
#include "../utils/debug_output.h"

/* =============================================================================
 * Internal Helper Functions
 * ========================================================================== */

/**
 * Escape a string for safe inclusion in JSON.
 * Returns an allocated string that must be freed by the caller.
 * Returns NULL on allocation failure.
 */
static char *json_escape_string_simple(const char *str) {
    if (str == NULL) {
        return strdup("");
    }

    /* Calculate the escaped length */
    size_t escaped_len = 0;
    for (const char *p = str; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\' || c == '/') {
            escaped_len += 2;  /* \x */
        } else if (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') {
            escaped_len += 2;  /* \x */
        } else if (c < 0x20) {
            escaped_len += 6;  /* \uXXXX */
        } else {
            escaped_len += 1;
        }
    }

    char *result = malloc(escaped_len + 1);
    if (result == NULL) {
        return NULL;
    }

    /* Build the escaped string */
    char *out = result;
    for (const char *p = str; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            *out++ = '\\';
            *out++ = '"';
        } else if (c == '\\') {
            *out++ = '\\';
            *out++ = '\\';
        } else if (c == '/') {
            *out++ = '\\';
            *out++ = '/';
        } else if (c == '\b') {
            *out++ = '\\';
            *out++ = 'b';
        } else if (c == '\f') {
            *out++ = '\\';
            *out++ = 'f';
        } else if (c == '\n') {
            *out++ = '\\';
            *out++ = 'n';
        } else if (c == '\r') {
            *out++ = '\\';
            *out++ = 'r';
        } else if (c == '\t') {
            *out++ = '\\';
            *out++ = 't';
        } else if (c < 0x20) {
            /* Control character - use \uXXXX format */
            out += sprintf(out, "\\u%04x", c);
        } else {
            *out++ = c;
        }
    }
    *out = '\0';

    return result;
}

/* Note: strcasestr_local() was removed - use standard strcasestr() where available,
 * or strstr() for case-sensitive matching. Cosmopolitan provides strcasestr(). */

/* Note: get_path_basename() was removed - use atomic_file_basename() from atomic_file.h */

/* =============================================================================
 * Constants and Static Data
 * ========================================================================== */

/* Default gate actions for each category */
static const GateAction DEFAULT_CATEGORY_ACTIONS[GATE_CATEGORY_COUNT] = {
    [GATE_CATEGORY_FILE_WRITE] = GATE_ACTION_GATE,
    [GATE_CATEGORY_FILE_READ]  = GATE_ACTION_ALLOW,
    [GATE_CATEGORY_SHELL]      = GATE_ACTION_GATE,
    [GATE_CATEGORY_NETWORK]    = GATE_ACTION_GATE,
    [GATE_CATEGORY_MEMORY]     = GATE_ACTION_ALLOW,
    [GATE_CATEGORY_SUBAGENT]   = GATE_ACTION_GATE,
    [GATE_CATEGORY_MCP]        = GATE_ACTION_GATE,
    [GATE_CATEGORY_PYTHON]     = GATE_ACTION_ALLOW
};

/* Category name strings */
static const char *CATEGORY_NAMES[GATE_CATEGORY_COUNT] = {
    [GATE_CATEGORY_FILE_WRITE] = "file_write",
    [GATE_CATEGORY_FILE_READ]  = "file_read",
    [GATE_CATEGORY_SHELL]      = "shell",
    [GATE_CATEGORY_NETWORK]    = "network",
    [GATE_CATEGORY_MEMORY]     = "memory",
    [GATE_CATEGORY_SUBAGENT]   = "subagent",
    [GATE_CATEGORY_MCP]        = "mcp",
    [GATE_CATEGORY_PYTHON]     = "python"
};

/* Action name strings */
static const char *ACTION_NAMES[] = {
    [GATE_ACTION_ALLOW] = "allow",
    [GATE_ACTION_GATE]  = "gate",
    [GATE_ACTION_DENY]  = "deny"
};

/* Approval result name strings */
static const char *RESULT_NAMES[] = {
    [APPROVAL_ALLOWED]        = "allowed",
    [APPROVAL_DENIED]         = "denied",
    [APPROVAL_NON_INTERACTIVE_DENIED] = "non_interactive_denied",
    [APPROVAL_ALLOWED_ALWAYS] = "allowed_always",
    [APPROVAL_ABORTED]        = "aborted",
    [APPROVAL_RATE_LIMITED]   = "rate_limited"
};

/* Note: verify_result_message() and VerifyResult messages are in atomic_file.c */

/* Initial capacities for dynamic arrays */
#define INITIAL_ALLOWLIST_CAPACITY 16
#define INITIAL_SHELL_ALLOWLIST_CAPACITY 16

/* Config file paths to search */
static const char *CONFIG_FILE_PATHS[] = {
    "./ralph.config.json",
    NULL
};

/* =============================================================================
 * Config Parsing Helpers
 * ========================================================================== */

/**
 * Parse an action string to GateAction enum.
 * Returns -1 if the string is not recognized.
 */
static int parse_gate_action(const char *str, GateAction *out) {
    if (str == NULL || out == NULL) {
        return -1;
    }

    if (strcmp(str, "allow") == 0) {
        *out = GATE_ACTION_ALLOW;
        return 0;
    } else if (strcmp(str, "gate") == 0) {
        *out = GATE_ACTION_GATE;
        return 0;
    } else if (strcmp(str, "deny") == 0) {
        *out = GATE_ACTION_DENY;
        return 0;
    }

    return -1;
}

/**
 * Parse a category name string to GateCategory enum.
 * Returns -1 if the string is not recognized.
 */
static int parse_gate_category(const char *str, GateCategory *out) {
    if (str == NULL || out == NULL) {
        return -1;
    }

    for (int i = 0; i < GATE_CATEGORY_COUNT; i++) {
        if (strcmp(str, CATEGORY_NAMES[i]) == 0) {
            *out = (GateCategory)i;
            return 0;
        }
    }

    return -1;
}


/**
 * Load approval gate config from a cJSON object.
 * This parses the "approval_gates" section of the config file.
 */
static int approval_gate_load_from_json(ApprovalGateConfig *config, cJSON *json) {
    if (config == NULL || json == NULL) {
        return -1;
    }

    cJSON *approval_gates = cJSON_GetObjectItem(json, "approval_gates");
    if (approval_gates == NULL || !cJSON_IsObject(approval_gates)) {
        /* No approval_gates section - use defaults */
        return 0;
    }

    /* Parse "enabled" field */
    cJSON *enabled = cJSON_GetObjectItem(approval_gates, "enabled");
    if (cJSON_IsBool(enabled)) {
        config->enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }

    /* Parse "categories" object */
    cJSON *categories = cJSON_GetObjectItem(approval_gates, "categories");
    if (cJSON_IsObject(categories)) {
        cJSON *cat_item = NULL;
        cJSON_ArrayForEach(cat_item, categories) {
            if (!cJSON_IsString(cat_item)) {
                continue;
            }

            GateCategory category;
            GateAction action;

            if (parse_gate_category(cat_item->string, &category) != 0) {
                continue;
            }

            if (parse_gate_action(cat_item->valuestring, &action) != 0) {
                continue;
            }

            config->categories[category] = action;
        }
    }

    /* Parse "allowlist" array */
    cJSON *allowlist = cJSON_GetObjectItem(approval_gates, "allowlist");
    if (cJSON_IsArray(allowlist)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, allowlist) {
            if (!cJSON_IsObject(entry)) {
                continue;
            }

            cJSON *tool = cJSON_GetObjectItem(entry, "tool");
            if (!cJSON_IsString(tool)) {
                continue;
            }

            const char *tool_name = tool->valuestring;

            /* Check if this is a shell command entry (has "command" array) */
            cJSON *command = cJSON_GetObjectItem(entry, "command");
            if (cJSON_IsArray(command)) {
                /* Shell command allowlist entry */
                int command_len = cJSON_GetArraySize(command);
                if (command_len <= 0) {
                    continue;
                }

                /* Allocate command prefix array */
                const char **command_prefix = calloc(command_len, sizeof(char *));
                if (command_prefix == NULL) {
                    continue;
                }

                int valid = 1;
                for (int i = 0; i < command_len; i++) {
                    cJSON *cmd_item = cJSON_GetArrayItem(command, i);
                    if (!cJSON_IsString(cmd_item)) {
                        valid = 0;
                        break;
                    }
                    command_prefix[i] = cmd_item->valuestring;
                }

                if (valid) {
                    /* Parse optional shell type */
                    cJSON *shell_type_json = cJSON_GetObjectItem(entry, "shell");
                    ShellType shell_type = SHELL_TYPE_UNKNOWN;
                    if (cJSON_IsString(shell_type_json)) {
                        parse_shell_type(shell_type_json->valuestring, &shell_type);
                    }

                    approval_gate_add_shell_allowlist(config, command_prefix,
                                                      command_len, shell_type);
                }

                free(command_prefix);
            } else {
                /* Regex pattern allowlist entry */
                cJSON *pattern = cJSON_GetObjectItem(entry, "pattern");
                if (cJSON_IsString(pattern)) {
                    approval_gate_add_allowlist(config, tool_name, pattern->valuestring);
                }
            }
        }
    }

    return 0;
}

/**
 * Load approval gate configuration from a JSON file.
 */
static int approval_gate_load_from_file(ApprovalGateConfig *config,
                                        const char *filepath) {
    if (config == NULL || filepath == NULL) {
        return -1;
    }

    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        return -1;
    }

    /* Read file content */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        return -1;
    }

    char *json_content = malloc(file_size + 1);
    if (json_content == NULL) {
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(json_content, 1, file_size, file);
    json_content[bytes_read] = '\0';
    fclose(file);

    /* Parse JSON */
    cJSON *json = cJSON_Parse(json_content);
    free(json_content);

    if (json == NULL) {
        return -1;
    }

    /* Load config from JSON */
    int result = approval_gate_load_from_json(config, json);
    cJSON_Delete(json);

    return result;
}

/* =============================================================================
 * Utility Functions
 * ========================================================================== */

const char *gate_category_name(GateCategory category) {
    if (category >= 0 && category < GATE_CATEGORY_COUNT) {
        return CATEGORY_NAMES[category];
    }
    return "unknown";
}

const char *gate_action_name(GateAction action) {
    if (action >= GATE_ACTION_ALLOW && action <= GATE_ACTION_DENY) {
        return ACTION_NAMES[action];
    }
    return "unknown";
}

const char *approval_result_name(ApprovalResult result) {
    if (result >= APPROVAL_ALLOWED && result <= APPROVAL_NON_INTERACTIVE_DENIED) {
        return RESULT_NAMES[result];
    }
    return "unknown";
}

/* verify_result_message() is implemented in atomic_file.c */

/* =============================================================================
 * Initialization and Cleanup
 * ========================================================================== */

int approval_gate_init(ApprovalGateConfig *config) {
    if (config == NULL) {
        return -1;
    }

    /* Zero-initialize the structure */
    memset(config, 0, sizeof(*config));

    /* Enable gates by default */
    config->enabled = 1;

    /* Default to non-interactive; call approval_gate_detect_interactive() to update */
    config->is_interactive = 0;

    /* Set default category actions */
    for (int i = 0; i < GATE_CATEGORY_COUNT; i++) {
        config->categories[i] = DEFAULT_CATEGORY_ACTIONS[i];
    }

    /* Initialize allowlist with initial capacity */
    config->allowlist = calloc(INITIAL_ALLOWLIST_CAPACITY, sizeof(AllowlistEntry));
    if (config->allowlist == NULL) {
        return -1;
    }
    config->allowlist_capacity = INITIAL_ALLOWLIST_CAPACITY;
    config->allowlist_count = 0;

    /* Initialize shell allowlist with initial capacity */
    config->shell_allowlist = calloc(INITIAL_SHELL_ALLOWLIST_CAPACITY,
                                     sizeof(ShellAllowEntry));
    if (config->shell_allowlist == NULL) {
        free(config->allowlist);
        config->allowlist = NULL;
        return -1;
    }
    config->shell_allowlist_capacity = INITIAL_SHELL_ALLOWLIST_CAPACITY;
    config->shell_allowlist_count = 0;

    /* Initialize rate limiter (opaque type) */
    config->rate_limiter = rate_limiter_create();
    if (config->rate_limiter == NULL) {
        free(config->shell_allowlist);
        free(config->allowlist);
        config->shell_allowlist = NULL;
        config->allowlist = NULL;
        return -1;
    }

    /* No approval channel for root process */
    config->approval_channel = NULL;

    /* Try to load configuration from config file */
    for (int i = 0; CONFIG_FILE_PATHS[i] != NULL; i++) {
        if (access(CONFIG_FILE_PATHS[i], R_OK) == 0) {
            int load_result = approval_gate_load_from_file(config, CONFIG_FILE_PATHS[i]);
            if (load_result != 0) {
                debug_printf("Warning: Failed to parse approval_gates from %s, "
                             "using defaults\n", CONFIG_FILE_PATHS[i]);
            }
            break;  /* Stop after first config file found */
        }
    }

    /* Record the count of static entries (from config file) for inheritance.
     * Session entries added via "allow always" will be added after this point,
     * so we can use these counts to distinguish static vs session entries. */
    config->static_allowlist_count = config->allowlist_count;
    config->static_shell_allowlist_count = config->shell_allowlist_count;

    return 0;
}

/**
 * Helper function to clean up partially initialized config on failure.
 * Used during approval_gate_init_from_parent() when allocation fails.
 */
static void cleanup_partial_child_config(ApprovalGateConfig *child) {
    if (child == NULL) {
        return;
    }

    /* Clean up shell allowlist */
    if (child->shell_allowlist != NULL) {
        for (int i = 0; i < child->shell_allowlist_count; i++) {
            ShellAllowEntry *entry = &child->shell_allowlist[i];
            if (entry->command_prefix != NULL) {
                for (int j = 0; j < entry->prefix_len; j++) {
                    free(entry->command_prefix[j]);
                }
                free(entry->command_prefix);
            }
        }
        free(child->shell_allowlist);
        child->shell_allowlist = NULL;
    }

    /* Clean up general allowlist */
    if (child->allowlist != NULL) {
        for (int i = 0; i < child->allowlist_count; i++) {
            free(child->allowlist[i].tool);
            free(child->allowlist[i].pattern);
            if (child->allowlist[i].valid) {
                regfree(&child->allowlist[i].compiled);
            }
        }
        free(child->allowlist);
        child->allowlist = NULL;
    }

    child->allowlist_count = 0;
    child->shell_allowlist_count = 0;
}

int approval_gate_init_from_parent(ApprovalGateConfig *child,
                                   const ApprovalGateConfig *parent) {
    if (child == NULL || parent == NULL) {
        return -1;
    }

    /* Zero-initialize the structure */
    memset(child, 0, sizeof(*child));

    /* Copy parent's enabled state and category configuration */
    child->enabled = parent->enabled;
    for (int i = 0; i < GATE_CATEGORY_COUNT; i++) {
        child->categories[i] = parent->categories[i];
    }

    /* Allocate and copy static allowlist entries from parent.
     * Only static entries (from config file) are inherited, NOT session entries
     * (runtime "allow always" entries). This is tracked by static_allowlist_count.
     * Validate static_count to prevent issues with corrupted data. */
    int static_count = parent->static_allowlist_count;
    if (static_count < 0) {
        static_count = 0;
    }

    if (static_count > 0) {
        child->allowlist = calloc(static_count, sizeof(AllowlistEntry));
        if (child->allowlist == NULL) {
            return -1;
        }
        child->allowlist_capacity = static_count;
        child->allowlist_count = 0;

        /* Deep copy each static allowlist entry with strdup failure checks */
        for (int i = 0; i < static_count; i++) {
            AllowlistEntry *src = &parent->allowlist[i];
            AllowlistEntry *dst = &child->allowlist[i];

            dst->valid = 0;

            /* Copy tool with strdup failure check */
            if (src->tool != NULL) {
                dst->tool = strdup(src->tool);
                if (dst->tool == NULL) {
                    cleanup_partial_child_config(child);
                    return -1;
                }
            } else {
                dst->tool = NULL;
            }

            /* Copy pattern with strdup failure check */
            if (src->pattern != NULL) {
                dst->pattern = strdup(src->pattern);
                if (dst->pattern == NULL) {
                    free(dst->tool);
                    dst->tool = NULL;
                    cleanup_partial_child_config(child);
                    return -1;
                }

                /* Recompile the regex for the child */
                if (regcomp(&dst->compiled, dst->pattern, REG_EXTENDED | REG_NOSUB) == 0) {
                    dst->valid = 1;
                }
            } else {
                dst->pattern = NULL;
            }

            child->allowlist_count++;
        }
    } else {
        /* No static entries - allocate minimal capacity */
        child->allowlist = calloc(INITIAL_ALLOWLIST_CAPACITY, sizeof(AllowlistEntry));
        if (child->allowlist == NULL) {
            return -1;
        }
        child->allowlist_capacity = INITIAL_ALLOWLIST_CAPACITY;
        child->allowlist_count = 0;
    }
    child->static_allowlist_count = child->allowlist_count;

    /* Allocate and copy static shell allowlist entries from parent.
     * Validate static_shell_count to prevent issues with corrupted data. */
    int static_shell_count = parent->static_shell_allowlist_count;
    if (static_shell_count < 0) {
        static_shell_count = 0;
    }

    if (static_shell_count > 0) {
        child->shell_allowlist = calloc(static_shell_count, sizeof(ShellAllowEntry));
        if (child->shell_allowlist == NULL) {
            cleanup_partial_child_config(child);
            return -1;
        }
        child->shell_allowlist_capacity = static_shell_count;
        child->shell_allowlist_count = 0;

        /* Deep copy each static shell allowlist entry with allocation failure checks */
        for (int i = 0; i < static_shell_count; i++) {
            ShellAllowEntry *src = &parent->shell_allowlist[i];
            ShellAllowEntry *dst = &child->shell_allowlist[i];

            dst->shell_type = src->shell_type;
            dst->prefix_len = src->prefix_len;
            dst->command_prefix = NULL;

            if (src->prefix_len > 0 && src->command_prefix != NULL) {
                dst->command_prefix = calloc(src->prefix_len, sizeof(char*));
                if (dst->command_prefix == NULL) {
                    dst->prefix_len = 0;
                    cleanup_partial_child_config(child);
                    return -1;
                }

                /* Copy each command prefix token with strdup failure check */
                for (int j = 0; j < src->prefix_len; j++) {
                    if (src->command_prefix[j] != NULL) {
                        dst->command_prefix[j] = strdup(src->command_prefix[j]);
                        if (dst->command_prefix[j] == NULL) {
                            /* Clean up already copied tokens */
                            for (int k = 0; k < j; k++) {
                                free(dst->command_prefix[k]);
                            }
                            free(dst->command_prefix);
                            dst->command_prefix = NULL;
                            dst->prefix_len = 0;
                            cleanup_partial_child_config(child);
                            return -1;
                        }
                    } else {
                        dst->command_prefix[j] = NULL;
                    }
                }
            }

            child->shell_allowlist_count++;
        }
    } else {
        /* No static entries - allocate minimal capacity */
        child->shell_allowlist = calloc(INITIAL_SHELL_ALLOWLIST_CAPACITY, sizeof(ShellAllowEntry));
        if (child->shell_allowlist == NULL) {
            cleanup_partial_child_config(child);
            return -1;
        }
        child->shell_allowlist_capacity = INITIAL_SHELL_ALLOWLIST_CAPACITY;
        child->shell_allowlist_count = 0;
    }
    child->static_shell_allowlist_count = child->shell_allowlist_count;

    /* Initialize rate limiter - subagents start fresh */
    child->rate_limiter = rate_limiter_create();
    if (child->rate_limiter == NULL) {
        cleanup_partial_child_config(child);
        return -1;
    }

    /* No approval channel set here - caller must set this */
    child->approval_channel = NULL;

    return 0;
}

static void free_allowlist_entry(AllowlistEntry *entry) {
    if (entry == NULL) {
        return;
    }
    free(entry->tool);
    free(entry->pattern);
    if (entry->valid) {
        regfree(&entry->compiled);
    }
    entry->tool = NULL;
    entry->pattern = NULL;
    entry->valid = 0;
}

static void free_shell_allow_entry(ShellAllowEntry *entry) {
    if (entry == NULL) {
        return;
    }
    if (entry->command_prefix != NULL) {
        for (int i = 0; i < entry->prefix_len; i++) {
            free(entry->command_prefix[i]);
        }
        free(entry->command_prefix);
    }
    entry->command_prefix = NULL;
    entry->prefix_len = 0;
}

/* free_denial_tracker removed - rate limiting is now handled by opaque RateLimiter type */

void approval_gate_cleanup(ApprovalGateConfig *config) {
    if (config == NULL) {
        return;
    }

    /* Free allowlist entries */
    if (config->allowlist != NULL) {
        for (int i = 0; i < config->allowlist_count; i++) {
            free_allowlist_entry(&config->allowlist[i]);
        }
        free(config->allowlist);
        config->allowlist = NULL;
    }

    /* Free shell allowlist entries */
    if (config->shell_allowlist != NULL) {
        for (int i = 0; i < config->shell_allowlist_count; i++) {
            free_shell_allow_entry(&config->shell_allowlist[i]);
        }
        free(config->shell_allowlist);
        config->shell_allowlist = NULL;
    }

    /* Destroy rate limiter */
    if (config->rate_limiter != NULL) {
        rate_limiter_destroy(config->rate_limiter);
        config->rate_limiter = NULL;
    }

    /* Clean up approval channel if present */
    if (config->approval_channel != NULL) {
        free_approval_channel(config->approval_channel);
        config->approval_channel = NULL;
    }

    config->allowlist_count = 0;
    config->allowlist_capacity = 0;
    config->shell_allowlist_count = 0;
    config->shell_allowlist_capacity = 0;
}

/* =============================================================================
 * Category and Tool Mapping
 * ========================================================================== */

GateCategory get_tool_category(const char *tool_name) {
    if (tool_name == NULL) {
        return GATE_CATEGORY_PYTHON; /* Default for unknown tools */
    }

    /* Check Python tool metadata first for dynamic tools with Gate: directive */
    if (is_python_file_tool(tool_name)) {
        const char *gate_category = python_tool_get_gate_category(tool_name);
        if (gate_category != NULL) {
            GateCategory category;
            if (parse_gate_category(gate_category, &category) == 0) {
                return category;
            }
        }
        /* Fall through to check hardcoded mappings for known tools */
    }

    /* Memory tools */
    if (strcmp(tool_name, "remember") == 0 ||
        strcmp(tool_name, "recall_memories") == 0 ||
        strcmp(tool_name, "forget_memory") == 0 ||
        strcmp(tool_name, "todo") == 0) {
        return GATE_CATEGORY_MEMORY;
    }

    /* Vector DB tools (prefix match) */
    if (strncmp(tool_name, "vector_db_", 10) == 0) {
        return GATE_CATEGORY_MEMORY;
    }

    /* MCP tools (prefix match) */
    if (strncmp(tool_name, "mcp_", 4) == 0) {
        return GATE_CATEGORY_MCP;
    }

    /* PDF tool - read operation */
    if (strcmp(tool_name, "process_pdf_document") == 0) {
        return GATE_CATEGORY_FILE_READ;
    }

    /* Python interpreter */
    if (strcmp(tool_name, "python") == 0) {
        return GATE_CATEGORY_PYTHON;
    }

    /* Subagent tools */
    if (strcmp(tool_name, "subagent") == 0 ||
        strcmp(tool_name, "subagent_status") == 0) {
        return GATE_CATEGORY_SUBAGENT;
    }

    /* Shell tool */
    if (strcmp(tool_name, "shell") == 0) {
        return GATE_CATEGORY_SHELL;
    }

    /* File read tools */
    if (strcmp(tool_name, "read_file") == 0 ||
        strcmp(tool_name, "file_info") == 0 ||
        strcmp(tool_name, "list_dir") == 0 ||
        strcmp(tool_name, "search_files") == 0) {
        return GATE_CATEGORY_FILE_READ;
    }

    /* File write tools */
    if (strcmp(tool_name, "write_file") == 0 ||
        strcmp(tool_name, "append_file") == 0 ||
        strcmp(tool_name, "apply_delta") == 0) {
        return GATE_CATEGORY_FILE_WRITE;
    }

    /* Network tools */
    if (strcmp(tool_name, "web_fetch") == 0) {
        return GATE_CATEGORY_NETWORK;
    }

    /* Default: Python/dynamic tools */
    return GATE_CATEGORY_PYTHON;
}

GateAction approval_gate_get_category_action(const ApprovalGateConfig *config,
                                             GateCategory category) {
    if (config == NULL || category < 0 || category >= GATE_CATEGORY_COUNT) {
        return GATE_ACTION_GATE; /* Default to gate on invalid input */
    }
    return config->categories[category];
}

/* =============================================================================
 * Rate Limiting (delegate to opaque RateLimiter)
 * ========================================================================== */

int is_rate_limited(const ApprovalGateConfig *config,
                    const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL || tool_call->name == NULL) {
        return 0;
    }
    return rate_limiter_is_blocked(config->rate_limiter, tool_call->name);
}

void track_denial(ApprovalGateConfig *config, const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL || tool_call->name == NULL) {
        return;
    }
    rate_limiter_record_denial(config->rate_limiter, tool_call->name);
}

void reset_denial_tracker(ApprovalGateConfig *config, const char *tool) {
    if (config == NULL || tool == NULL) {
        return;
    }
    rate_limiter_reset(config->rate_limiter, tool);
}

int get_rate_limit_remaining(const ApprovalGateConfig *config,
                             const char *tool) {
    if (config == NULL || tool == NULL) {
        return 0;
    }
    return rate_limiter_get_remaining(config->rate_limiter, tool);
}

/* =============================================================================
 * Allowlist Management
 * ========================================================================== */

int approval_gate_add_allowlist(ApprovalGateConfig *config,
                                const char *tool,
                                const char *pattern) {
    if (config == NULL || tool == NULL || pattern == NULL) {
        return -1;
    }

    /* Check if we need to grow the array */
    if (config->allowlist_count >= config->allowlist_capacity) {
        int new_capacity = config->allowlist_capacity * 2;
        AllowlistEntry *new_list = realloc(config->allowlist,
                                           new_capacity * sizeof(AllowlistEntry));
        if (new_list == NULL) {
            return -1;
        }
        config->allowlist = new_list;
        config->allowlist_capacity = new_capacity;
    }

    /* Add new entry */
    AllowlistEntry *entry = &config->allowlist[config->allowlist_count];
    memset(entry, 0, sizeof(*entry));

    entry->tool = strdup(tool);
    if (entry->tool == NULL) {
        return -1;
    }

    entry->pattern = strdup(pattern);
    if (entry->pattern == NULL) {
        free(entry->tool);
        entry->tool = NULL;
        return -1;
    }

    /* Compile the regex */
    int ret = regcomp(&entry->compiled, pattern, REG_EXTENDED | REG_NOSUB);
    if (ret != 0) {
        /* Regex compilation failed - still add entry but mark as invalid */
        entry->valid = 0;
    } else {
        entry->valid = 1;
    }

    config->allowlist_count++;
    return 0;
}

int approval_gate_add_shell_allowlist(ApprovalGateConfig *config,
                                      const char **command_prefix,
                                      int prefix_len,
                                      ShellType shell_type) {
    if (config == NULL || command_prefix == NULL || prefix_len <= 0) {
        return -1;
    }

    /* Check if we need to grow the array */
    if (config->shell_allowlist_count >= config->shell_allowlist_capacity) {
        int new_capacity = config->shell_allowlist_capacity * 2;
        ShellAllowEntry *new_list = realloc(config->shell_allowlist,
                                            new_capacity * sizeof(ShellAllowEntry));
        if (new_list == NULL) {
            return -1;
        }
        config->shell_allowlist = new_list;
        config->shell_allowlist_capacity = new_capacity;
    }

    /* Add new entry */
    ShellAllowEntry *entry = &config->shell_allowlist[config->shell_allowlist_count];
    memset(entry, 0, sizeof(*entry));

    entry->command_prefix = calloc(prefix_len, sizeof(char *));
    if (entry->command_prefix == NULL) {
        return -1;
    }

    for (int i = 0; i < prefix_len; i++) {
        entry->command_prefix[i] = strdup(command_prefix[i]);
        if (entry->command_prefix[i] == NULL) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                free(entry->command_prefix[j]);
            }
            free(entry->command_prefix);
            entry->command_prefix = NULL;
            return -1;
        }
    }

    entry->prefix_len = prefix_len;
    entry->shell_type = shell_type;

    config->shell_allowlist_count++;
    return 0;
}

/**
 * Extract the match target value from tool call arguments.
 * For Python file tools, this uses the Match: directive to identify which argument
 * to extract. Returns an allocated string that must be freed by the caller.
 */
static char *extract_match_target(const char *tool_name, const char *arguments_json) {
    if (tool_name == NULL || arguments_json == NULL) {
        return NULL;
    }

    /* Check if it's a Python file tool with a Match: directive */
    if (is_python_file_tool(tool_name)) {
        const char *match_arg = python_tool_get_match_arg(tool_name);
        if (match_arg != NULL) {
            /* Parse arguments JSON and extract the specified argument */
            cJSON *args = cJSON_Parse(arguments_json);
            if (args != NULL) {
                cJSON *target_item = cJSON_GetObjectItem(args, match_arg);
                char *result = NULL;
                if (cJSON_IsString(target_item) && target_item->valuestring != NULL) {
                    result = strdup(target_item->valuestring);
                }
                cJSON_Delete(args);
                return result;
            }
        }
    }

    /* Fall back to using full arguments JSON as match target */
    return strdup(arguments_json);
}

static int match_regex_allowlist(const ApprovalGateConfig *config,
                                 const char *tool_name,
                                 const char *match_target) {
    if (config == NULL || tool_name == NULL || match_target == NULL) {
        return 0;
    }

    for (int i = 0; i < config->allowlist_count; i++) {
        const AllowlistEntry *entry = &config->allowlist[i];
        if (!entry->valid) {
            continue;
        }
        if (strcmp(entry->tool, tool_name) != 0) {
            continue;
        }

        /* Match the pattern against the target */
        if (regexec(&entry->compiled, match_target, 0, NULL, 0) == 0) {
            return 1; /* Match found */
        }
    }

    return 0;
}

/**
 * Match a shell command against the shell allowlist.
 *
 * Shell commands use parsed command prefix matching rather than regex.
 * This function:
 * 1. Extracts the "command" field from the tool call arguments JSON
 * 2. Parses the command using the shell parser
 * 3. Checks against each shell allowlist entry
 * 4. Handles shell-type-specific entries and command equivalence
 *
 * Commands with chain operators, pipes, subshells, or dangerous patterns
 * NEVER match the allowlist.
 */
static int match_shell_command_allowlist(const ApprovalGateConfig *config,
                                         const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL) {
        return 0;
    }

    /* No shell allowlist entries to check */
    if (config->shell_allowlist_count == 0 || config->shell_allowlist == NULL) {
        return 0;
    }

    /* Extract the command from arguments JSON */
    if (tool_call->arguments == NULL) {
        return 0;
    }

    cJSON *args = cJSON_Parse(tool_call->arguments);
    if (args == NULL) {
        return 0;
    }

    cJSON *command_item = cJSON_GetObjectItem(args, "command");
    if (!cJSON_IsString(command_item) || command_item->valuestring == NULL) {
        cJSON_Delete(args);
        return 0;
    }

    const char *command_str = command_item->valuestring;

    /* Parse the shell command */
    ParsedShellCommand *parsed = parse_shell_command(command_str);
    if (parsed == NULL) {
        cJSON_Delete(args);
        return 0;
    }

    /* Commands with chain operators, pipes, subshells, or dangerous patterns
     * NEVER match the allowlist */
    if (!shell_command_is_safe_for_matching(parsed)) {
        free_parsed_shell_command(parsed);
        cJSON_Delete(args);
        return 0;
    }

    /* Get the base command for equivalence checking */
    const char *base_cmd = shell_command_get_base(parsed);
    if (base_cmd == NULL) {
        free_parsed_shell_command(parsed);
        cJSON_Delete(args);
        return 0;
    }

    /* Check against each shell allowlist entry */
    int matched = 0;
    for (int i = 0; i < config->shell_allowlist_count && !matched; i++) {
        const ShellAllowEntry *entry = &config->shell_allowlist[i];

        if (entry->prefix_len <= 0 || entry->command_prefix == NULL) {
            continue;
        }

        /* Check shell type compatibility */
        if (entry->shell_type != SHELL_TYPE_UNKNOWN) {
            /* Entry is for a specific shell type */
            if (entry->shell_type != parsed->shell_type) {
                continue;  /* Shell types don't match */
            }
        }

        /* First, try direct prefix matching */
        if (shell_command_matches_prefix(parsed,
                                         (const char * const *)entry->command_prefix,
                                         entry->prefix_len)) {
            matched = 1;
            break;
        }

        /* If entry has SHELL_TYPE_UNKNOWN (any shell), try command equivalence
         * for the base command only */
        if (entry->shell_type == SHELL_TYPE_UNKNOWN && entry->prefix_len >= 1) {
            if (commands_are_equivalent(entry->command_prefix[0], base_cmd)) {
                /* Base command is equivalent - check if rest of prefix matches */
                if (entry->prefix_len == 1) {
                    /* Single-token entry, base command equivalence is enough */
                    matched = 1;
                } else if (parsed->token_count >= entry->prefix_len) {
                    /* Multi-token entry: first token matches via equivalence,
                     * check remaining tokens for exact match */
                    int prefix_match = 1;
                    for (int j = 1; j < entry->prefix_len; j++) {
                        if (strcmp(parsed->tokens[j], entry->command_prefix[j]) != 0) {
                            prefix_match = 0;
                            break;
                        }
                    }
                    matched = prefix_match;
                }
            }
        }
    }

    free_parsed_shell_command(parsed);
    cJSON_Delete(args);
    return matched;
}

int approval_gate_matches_allowlist(const ApprovalGateConfig *config,
                                    const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL || tool_call->name == NULL) {
        return 0;
    }

    GateCategory category = get_tool_category(tool_call->name);

    /* Shell commands use parsed command prefix matching */
    if (category == GATE_CATEGORY_SHELL) {
        return match_shell_command_allowlist(config, tool_call);
    }

    /* For other tools, extract the match target based on Match: directive */
    if (tool_call->arguments != NULL) {
        char *match_target = extract_match_target(tool_call->name, tool_call->arguments);
        if (match_target != NULL) {
            int result = match_regex_allowlist(config, tool_call->name, match_target);
            free(match_target);
            return result;
        }
    }

    return 0;
}

/* =============================================================================
 * Approval Checking
 * ========================================================================== */

int approval_gate_requires_check(const ApprovalGateConfig *config,
                                 const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL) {
        return -1; /* Error */
    }

    /* If gates are disabled, everything is allowed */
    if (!config->enabled) {
        return 0;
    }

    GateCategory category = get_tool_category(tool_call->name);
    GateAction action = approval_gate_get_category_action(config, category);

    switch (action) {
        case GATE_ACTION_ALLOW:
            return 0; /* Allowed without approval */

        case GATE_ACTION_DENY:
            return -1; /* Denied, never allowed */

        case GATE_ACTION_GATE:
            /* Check allowlist first */
            if (approval_gate_matches_allowlist(config, tool_call)) {
                return 0; /* Matches allowlist, allowed */
            }
            return 1; /* Requires approval */

        default:
            return 1; /* Unknown action, require approval */
    }
}

/**
 * Extract the shell command from tool call arguments.
 * Returns allocated string or NULL if not a shell command.
 */
static char *extract_shell_command(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->name == NULL) {
        return NULL;
    }
    if (strcmp(tool_call->name, "shell") != 0) {
        return NULL;
    }
    return tool_args_get_command(tool_call);
}

/**
 * Extract file path from tool call arguments.
 * Returns allocated string or NULL if not available.
 */
static char *extract_file_path(const ToolCall *tool_call) {
    return tool_args_get_path(tool_call);
}

ApprovalResult approval_gate_prompt(ApprovalGateConfig *config,
                                    const ToolCall *tool_call,
                                    ApprovedPath *out_path) {
    if (config == NULL || tool_call == NULL) {
        return APPROVAL_DENIED;
    }

    /* Create prompter - returns NULL if no TTY */
    GatePrompter *gp = gate_prompter_create();
    if (gp == NULL) {
        return APPROVAL_NON_INTERACTIVE_DENIED;
    }

    /* Initialize out_path if provided */
    if (out_path != NULL) {
        memset(out_path, 0, sizeof(*out_path));
    }

    /* Extract display information based on tool type */
    char *shell_command = extract_shell_command(tool_call);
    char *file_path = extract_file_path(tool_call);
    GateCategory category = get_tool_category(tool_call->name);

    /* Determine what to show as primary detail */
    const char *command_arg = NULL;
    const char *path_arg = NULL;
    if (shell_command != NULL) {
        command_arg = shell_command;
    } else if (file_path != NULL && (category == GATE_CATEGORY_FILE_READ ||
                                      category == GATE_CATEGORY_FILE_WRITE)) {
        path_arg = file_path;
    }

    ApprovalResult result = APPROVAL_DENIED;

    /* Iterative prompt loop */
    for (;;) {
        /* Display the prompt using gate_prompter */
        fprintf(stderr, "\n");
        gate_prompter_show_single(gp, tool_call, command_arg, path_arg);

        /* Read single keypress */
        int response = gate_prompter_read_key(gp);
        gate_prompter_newline(gp);

        /* Handle the response */
        if (response < 0) {
            result = APPROVAL_ABORTED;
            break;
        }

        switch (tolower(response)) {
            case 'y':
                reset_denial_tracker(config, tool_call->name);
                result = APPROVAL_ALLOWED;
                goto done;

            case 'n':
                result = APPROVAL_DENIED;
                goto done;

            case 'a':
                reset_denial_tracker(config, tool_call->name);
                result = APPROVAL_ALLOWED_ALWAYS;
                goto done;

            case '?': {
                /* Show details using gate_prompter */
                char *resolved = out_path ? out_path->resolved_path : NULL;
                int path_exists = out_path ? out_path->existed : 0;
                gate_prompter_show_details(gp, tool_call, resolved, path_exists);
                gate_prompter_read_key(gp);  /* Wait for keypress */
                continue;
            }

            case 3: /* Ctrl+C */
            case 4: /* Ctrl+D */
                result = APPROVAL_ABORTED;
                goto done;

            default:
                fprintf(stderr, "Invalid input. Press y, n, a, or ? for details.\n");
                continue;
        }
    }

done:
    free(shell_command);
    free(file_path);
    gate_prompter_destroy(gp);
    return result;
}

ApprovalResult check_approval_gate(ApprovalGateConfig *config,
                                   const ToolCall *tool_call,
                                   ApprovedPath *out_path) {
    if (config == NULL || tool_call == NULL) {
        return APPROVAL_DENIED;
    }

    /* Initialize out_path if provided */
    if (out_path != NULL) {
        memset(out_path, 0, sizeof(*out_path));
    }

    /* Check rate limiting first */
    if (is_rate_limited(config, tool_call)) {
        return APPROVAL_RATE_LIMITED;
    }

    /* Check if approval is required */
    int check_result = approval_gate_requires_check(config, tool_call);

    switch (check_result) {
        case 0:
            /* Allowed without prompt */
            return APPROVAL_ALLOWED;

        case -1:
            /* Denied by configuration */
            return APPROVAL_DENIED;

        case 1:
            /* Requires approval - check if we're a subagent */
            {
                ApprovalChannel *channel = subagent_get_approval_channel();
                if (channel != NULL) {
                    /* We're a subagent - route through IPC to parent */
                    return subagent_request_approval(channel, tool_call, out_path);
                }

                /* Not a subagent - check if we're in interactive mode */
                if (!config->is_interactive) {
                    /* Non-interactive mode: can't prompt, so deny with specific error */
                    return APPROVAL_NON_INTERACTIVE_DENIED;
                }

                /* Interactive mode - prompt user directly via TTY */
                return approval_gate_prompt(config, tool_call, out_path);
            }

        default:
            return APPROVAL_DENIED;
    }
}

/* =============================================================================
 * Batch Approval
 * ========================================================================== */

int init_batch_result(ApprovalBatchResult *batch, int count) {
    if (batch == NULL || count <= 0) {
        return -1;
    }

    memset(batch, 0, sizeof(*batch));

    batch->results = calloc(count, sizeof(ApprovalResult));
    if (batch->results == NULL) {
        return -1;
    }

    batch->paths = calloc(count, sizeof(ApprovedPath));
    if (batch->paths == NULL) {
        free(batch->results);
        batch->results = NULL;
        return -1;
    }

    batch->count = count;

    /* Initialize all results to DENIED (safe default) */
    for (int i = 0; i < count; i++) {
        batch->results[i] = APPROVAL_DENIED;
        memset(&batch->paths[i], 0, sizeof(ApprovedPath));
    }

    return 0;
}

void free_batch_result(ApprovalBatchResult *batch) {
    if (batch == NULL) {
        return;
    }

    /* Free each approved path */
    if (batch->paths != NULL) {
        for (int i = 0; i < batch->count; i++) {
            free_approved_path(&batch->paths[i]);
        }
        free(batch->paths);
        batch->paths = NULL;
    }

    free(batch->results);
    batch->results = NULL;
    batch->count = 0;
}

ApprovalResult approval_gate_prompt_batch(ApprovalGateConfig *config,
                                          const ToolCall *tool_calls,
                                          int count,
                                          ApprovalBatchResult *out_batch) {
    if (config == NULL || tool_calls == NULL || count <= 0 || out_batch == NULL) {
        return APPROVAL_DENIED;
    }

    /* Create prompter - returns NULL if no TTY */
    GatePrompter *gp = gate_prompter_create();
    if (gp == NULL) {
        return APPROVAL_NON_INTERACTIVE_DENIED;
    }

    /* Initialize batch result */
    if (init_batch_result(out_batch, count) != 0) {
        gate_prompter_destroy(gp);
        return APPROVAL_DENIED;
    }

    /* Track which operations are pending and build status string */
    int *pending = calloc(count, sizeof(int));
    char *statuses = calloc(count + 1, sizeof(char));
    if (pending == NULL || statuses == NULL) {
        free(pending);
        free(statuses);
        free_batch_result(out_batch);
        gate_prompter_destroy(gp);
        return APPROVAL_DENIED;
    }

    /* Initially all are pending */
    int pending_count = count;
    for (int i = 0; i < count; i++) {
        pending[i] = 1;
        statuses[i] = ' ';
    }

    ApprovalResult result = APPROVAL_DENIED;

    /* Interactive batch prompt loop */
    for (;;) {
        /* Display batch prompt using gate_prompter */
        fprintf(stderr, "\n");
        gate_prompter_show_batch(gp, tool_calls, count,
                                 pending_count < count ? statuses : NULL);

        /* Read user input */
        int response = gate_prompter_read_key(gp);
        gate_prompter_newline(gp);

        if (response < 0) {
            free_batch_result(out_batch);
            result = APPROVAL_ABORTED;
            goto done;
        }

        switch (tolower(response)) {
            case 'y':
                /* Allow all pending operations */
                for (int i = 0; i < count; i++) {
                    if (pending[i]) {
                        out_batch->results[i] = APPROVAL_ALLOWED;
                        reset_denial_tracker(config, tool_calls[i].name);
                    }
                }
                result = APPROVAL_ALLOWED;
                goto done;

            case 'n':
                /* Deny all pending operations */
                for (int i = 0; i < count; i++) {
                    if (pending[i]) {
                        out_batch->results[i] = APPROVAL_DENIED;
                    }
                }
                result = APPROVAL_DENIED;
                goto done;

            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9': {
                /* Parse operation number (handles multi-digit) */
                int op_num = response - '0';
                if (count > 9) {
                    /* May need more digits - read with timeout */
                    char next_key;
                    int got_key = gate_prompter_read_key_timeout(gp, 500, &next_key);
                    if (got_key == 1 && next_key >= '0' && next_key <= '9') {
                        op_num = op_num * 10 + (next_key - '0');
                    }
                }

                if (op_num < 1 || op_num > count) {
                    fprintf(stderr, "Invalid operation number. Enter 1-%d.\n", count);
                    continue;
                }

                int idx = op_num - 1;
                if (!pending[idx]) {
                    fprintf(stderr, "Operation %d already processed.\n", op_num);
                    continue;
                }

                /* Prompt for individual operation */
                ApprovalResult single_result = approval_gate_prompt(
                    config, &tool_calls[idx], &out_batch->paths[idx]);

                if (single_result == APPROVAL_ABORTED) {
                    free_batch_result(out_batch);
                    result = APPROVAL_ABORTED;
                    goto done;
                }

                out_batch->results[idx] = single_result;
                pending[idx] = 0;
                pending_count--;

                /* Update status char */
                if (single_result == APPROVAL_ALLOWED ||
                    single_result == APPROVAL_ALLOWED_ALWAYS) {
                    statuses[idx] = '+';
                } else {
                    statuses[idx] = '-';
                }

                /* Check if all operations have been reviewed */
                if (pending_count == 0) {
                    int any_denied = 0;
                    int all_always = 1;
                    for (int i = 0; i < count; i++) {
                        if (out_batch->results[i] == APPROVAL_DENIED) {
                            any_denied = 1;
                        }
                        if (out_batch->results[i] != APPROVAL_ALLOWED_ALWAYS) {
                            all_always = 0;
                        }
                    }
                    if (any_denied) {
                        result = APPROVAL_DENIED;
                    } else if (all_always) {
                        result = APPROVAL_ALLOWED_ALWAYS;
                    } else {
                        result = APPROVAL_ALLOWED;
                    }
                    goto done;
                }
                continue;
            }

            case 3: /* Ctrl+C */
            case 4: /* Ctrl+D */
                free_batch_result(out_batch);
                result = APPROVAL_ABORTED;
                goto done;

            default:
                fprintf(stderr, "Invalid input. Press y, n, or 1-%d.\n", count);
                continue;
        }
    }

done:
    free(pending);
    free(statuses);
    gate_prompter_destroy(gp);
    return result;
}

ApprovalResult check_approval_gate_batch(ApprovalGateConfig *config,
                                         const ToolCall *tool_calls,
                                         int count,
                                         ApprovalBatchResult *out_batch) {
    if (config == NULL || tool_calls == NULL || count <= 0 || out_batch == NULL) {
        return APPROVAL_DENIED;
    }

    /* Initialize batch result */
    if (init_batch_result(out_batch, count) != 0) {
        return APPROVAL_DENIED;
    }

    /* First pass: check which tool calls need approval */
    int *needs_approval = calloc(count, sizeof(int));
    int *needs_approval_indices = calloc(count, sizeof(int));
    if (needs_approval == NULL || needs_approval_indices == NULL) {
        free(needs_approval);
        free(needs_approval_indices);
        free_batch_result(out_batch);
        return APPROVAL_DENIED;
    }

    int approval_count = 0;
    int any_rate_limited = 0;
    int any_denied = 0;

    for (int i = 0; i < count; i++) {
        /* Check rate limiting first */
        if (is_rate_limited(config, &tool_calls[i])) {
            out_batch->results[i] = APPROVAL_RATE_LIMITED;
            any_rate_limited = 1;
            continue;
        }

        /* Check if approval is required */
        int check_result = approval_gate_requires_check(config, &tool_calls[i]);

        switch (check_result) {
            case 0:
                /* Allowed without prompt */
                out_batch->results[i] = APPROVAL_ALLOWED;
                break;

            case -1:
                /* Denied by configuration */
                out_batch->results[i] = APPROVAL_DENIED;
                any_denied = 1;
                break;

            case 1:
                /* Requires approval */
                needs_approval[i] = 1;
                needs_approval_indices[approval_count++] = i;
                break;

            default:
                out_batch->results[i] = APPROVAL_DENIED;
                any_denied = 1;
                break;
        }
    }

    /* If no approvals needed, return early */
    if (approval_count == 0) {
        free(needs_approval);
        free(needs_approval_indices);
        if (any_rate_limited) {
            return APPROVAL_RATE_LIMITED;
        }
        if (any_denied) {
            return APPROVAL_DENIED;
        }
        return APPROVAL_ALLOWED;
    }

    /* Check for non-interactive mode before prompting */
    if (!config->is_interactive) {
        /* Non-interactive mode: can't prompt, so deny all pending approvals */
        for (int i = 0; i < approval_count; i++) {
            int idx = needs_approval_indices[i];
            out_batch->results[idx] = APPROVAL_NON_INTERACTIVE_DENIED;
        }
        free(needs_approval);
        free(needs_approval_indices);
        return APPROVAL_NON_INTERACTIVE_DENIED;
    }

    /* If only one approval needed, use single prompt */
    if (approval_count == 1) {
        int idx = needs_approval_indices[0];
        ApprovalResult result = approval_gate_prompt(
            config, &tool_calls[idx], &out_batch->paths[idx]);
        out_batch->results[idx] = result;

        free(needs_approval);
        free(needs_approval_indices);

        if (result == APPROVAL_ABORTED) {
            return APPROVAL_ABORTED;
        }
        if (result == APPROVAL_DENIED || any_denied) {
            return APPROVAL_DENIED;
        }
        if (any_rate_limited) {
            return APPROVAL_RATE_LIMITED;
        }
        return result;
    }

    /* Build array of tool calls that need approval */
    ToolCall *approval_calls = calloc(approval_count, sizeof(ToolCall));
    if (approval_calls == NULL) {
        free(needs_approval);
        free(needs_approval_indices);
        free_batch_result(out_batch);
        return APPROVAL_DENIED;
    }

    for (int i = 0; i < approval_count; i++) {
        int idx = needs_approval_indices[i];
        approval_calls[i] = tool_calls[idx];
    }

    /* Create temporary batch for approval-needing calls */
    ApprovalBatchResult temp_batch;
    ApprovalResult batch_result = approval_gate_prompt_batch(
        config, approval_calls, approval_count, &temp_batch);

    /* Copy results back to out_batch */
    for (int i = 0; i < approval_count; i++) {
        int idx = needs_approval_indices[i];
        out_batch->results[idx] = temp_batch.results[i];
        /* Copy approved path */
        out_batch->paths[idx] = temp_batch.paths[i];
        /* Zero out temp to prevent double-free */
        memset(&temp_batch.paths[i], 0, sizeof(ApprovedPath));
    }

    free_batch_result(&temp_batch);
    free(approval_calls);
    free(needs_approval);
    free(needs_approval_indices);

    if (batch_result == APPROVAL_ABORTED) {
        return APPROVAL_ABORTED;
    }

    /* Determine overall status */
    if (any_rate_limited) {
        return APPROVAL_RATE_LIMITED;
    }
    if (any_denied || batch_result == APPROVAL_DENIED) {
        return APPROVAL_DENIED;
    }

    return batch_result;
}

/* Path verification functions (verify_approved_path, verify_and_open_approved_path,
 * free_approved_path) are implemented in atomic_file.c */

/* =============================================================================
 * Subagent Approval Proxy
 * ========================================================================== */

/* Full implementations are in subagent_approval.c */

/* =============================================================================
 * Error Formatting
 * ========================================================================== */

char *format_rate_limit_error(const ApprovalGateConfig *config,
                              const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL) {
        return NULL;
    }

    int remaining = get_rate_limit_remaining(config, tool_call->name);
    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

    /* Escape tool name for JSON */
    char *escaped_tool = json_escape_string_simple(tool_name);
    if (escaped_tool == NULL) {
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"rate_limited\", "
                       "\"message\": \"Too many denied requests for %s tool. "
                       "Wait %d seconds before retrying.\", "
                       "\"retry_after\": %d, "
                       "\"tool\": \"%s\"}",
                       escaped_tool, remaining, remaining, escaped_tool);

    free(escaped_tool);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

char *format_denial_error(const ToolCall *tool_call) {
    if (tool_call == NULL) {
        return NULL;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

    /* Escape tool name for JSON */
    char *escaped_tool = json_escape_string_simple(tool_name);
    if (escaped_tool == NULL) {
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"operation_denied\", "
                       "\"message\": \"User denied permission to execute %s\", "
                       "\"tool\": \"%s\", "
                       "\"suggestion\": \"Ask the user to perform this operation "
                       "manually, or request permission with explanation\"}",
                       escaped_tool, escaped_tool);

    free(escaped_tool);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

char *format_protected_file_error(const char *path) {
    if (path == NULL) {
        path = "unknown";
    }

    /* Escape path for JSON */
    char *escaped_path = json_escape_string_simple(path);
    if (escaped_path == NULL) {
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"protected_file\", "
                       "\"message\": \"Cannot modify protected configuration file\", "
                       "\"path\": \"%s\"}",
                       escaped_path);

    free(escaped_path);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

char *format_non_interactive_error(const ToolCall *tool_call) {
    if (tool_call == NULL) {
        return NULL;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";
    GateCategory category = get_tool_category(tool_name);
    const char *category_name = gate_category_name(category);

    /* Escape tool name and category for JSON */
    char *escaped_tool = json_escape_string_simple(tool_name);
    char *escaped_category = json_escape_string_simple(category_name);
    if (escaped_tool == NULL || escaped_category == NULL) {
        free(escaped_tool);
        free(escaped_category);
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"non_interactive_gate\", "
                       "\"message\": \"Cannot execute %s operation without TTY for approval\", "
                       "\"tool\": \"%s\", "
                       "\"category\": \"%s\", "
                       "\"suggestion\": \"Use --yolo to bypass gates, or "
                       "--allow-category=%s to allow this category in non-interactive mode\"}",
                       escaped_category, escaped_tool, escaped_category, escaped_category);

    free(escaped_tool);
    free(escaped_category);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

/* format_verify_error() is implemented in atomic_file.c */

/* =============================================================================
 * CLI Override Functions
 * ========================================================================== */

void approval_gate_enable_yolo(ApprovalGateConfig *config) {
    if (config == NULL) {
        return;
    }
    config->enabled = 0;
}

int approval_gate_set_category_action(ApprovalGateConfig *config,
                                      const char *category_name,
                                      GateAction action) {
    if (config == NULL || category_name == NULL) {
        return -1;
    }

    GateCategory category;
    if (parse_gate_category(category_name, &category) != 0) {
        return -1;
    }

    config->categories[category] = action;
    return 0;
}

int approval_gate_parse_category(const char *name, GateCategory *out_category) {
    return parse_gate_category(name, out_category);
}

int approval_gate_add_cli_allow(ApprovalGateConfig *config,
                                const char *allow_spec) {
    if (config == NULL || allow_spec == NULL) {
        return -1;
    }

    /* Find the colon separator between tool and arguments */
    const char *colon = strchr(allow_spec, ':');
    if (colon == NULL) {
        /* No colon - invalid format */
        return -1;
    }

    /* Extract tool name */
    size_t tool_len = colon - allow_spec;
    if (tool_len == 0) {
        return -1;
    }

    char *tool_name = malloc(tool_len + 1);
    if (tool_name == NULL) {
        return -1;
    }
    memcpy(tool_name, allow_spec, tool_len);
    tool_name[tool_len] = '\0';

    /* Get the arguments part (after the colon) */
    const char *args = colon + 1;
    if (*args == '\0') {
        /* No arguments after colon */
        free(tool_name);
        return -1;
    }

    int result;

    if (strcmp(tool_name, "shell") == 0) {
        /* For shell, parse comma-separated command tokens */
        /* Count the number of tokens */
        int token_count = 1;
        for (const char *p = args; *p != '\0'; p++) {
            if (*p == ',') {
                token_count++;
            }
        }

        /* Allocate token array */
        const char **tokens = calloc(token_count, sizeof(char *));
        char **allocated_tokens = calloc(token_count, sizeof(char *));
        if (tokens == NULL || allocated_tokens == NULL) {
            free(tool_name);
            free(tokens);
            free(allocated_tokens);
            return -1;
        }

        /* Parse tokens */
        char *args_copy = strdup(args);
        if (args_copy == NULL) {
            free(tool_name);
            free(tokens);
            free(allocated_tokens);
            return -1;
        }

        int idx = 0;
        char *saveptr = NULL;
        char *token = strtok_r(args_copy, ",", &saveptr);
        while (token != NULL && idx < token_count) {
            allocated_tokens[idx] = strdup(token);
            if (allocated_tokens[idx] == NULL) {
                /* Cleanup on failure */
                for (int i = 0; i < idx; i++) {
                    free(allocated_tokens[i]);
                }
                free(args_copy);
                free(tool_name);
                free(tokens);
                free(allocated_tokens);
                return -1;
            }
            tokens[idx] = allocated_tokens[idx];
            idx++;
            token = strtok_r(NULL, ",", &saveptr);
        }

        /* Add to shell allowlist */
        result = approval_gate_add_shell_allowlist(config, tokens, idx,
                                                   SHELL_TYPE_UNKNOWN);

        /* Cleanup */
        for (int i = 0; i < idx; i++) {
            free(allocated_tokens[i]);
        }
        free(args_copy);
        free(tokens);
        free(allocated_tokens);
    } else {
        /* For other tools, treat args as a regex pattern */
        result = approval_gate_add_allowlist(config, tool_name, args);
    }

    free(tool_name);
    return result;
}

/* =============================================================================
 * Non-Interactive Mode Detection
 * ========================================================================== */

void approval_gate_detect_interactive(ApprovalGateConfig *config) {
    if (config == NULL) {
        return;
    }

    /* Check if stdin is a TTY */
    config->is_interactive = isatty(STDIN_FILENO) ? 1 : 0;
}

int approval_gate_is_interactive(const ApprovalGateConfig *config) {
    if (config == NULL) {
        return 0;
    }
    return config->is_interactive;
}

/* Pattern generation functions are implemented in pattern_generator.c */
