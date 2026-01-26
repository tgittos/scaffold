/**
 * Approval Gates Implementation
 *
 * This module provides the core approval gate logic for requiring user
 * confirmation before executing potentially destructive operations.
 *
 * See SPEC_APPROVAL_GATES.md for the full specification.
 */

#include "approval_gate.h"
#include "shell_parser.h"
#include "subagent_approval.h"

#include <cJSON.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "../tools/python_tool_files.h"
#include "../tools/subagent_tool.h"
#include "../utils/debug_output.h"

/* Signal flag for Ctrl+C during prompt */
static volatile sig_atomic_t g_prompt_interrupted = 0;

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
    [APPROVAL_ALLOWED_ALWAYS] = "allowed_always",
    [APPROVAL_ABORTED]        = "aborted",
    [APPROVAL_RATE_LIMITED]   = "rate_limited"
};

/* Note: verify_result_message() and VerifyResult messages are in atomic_file.c */

/* Rate limiting backoff schedule (in seconds) */
static const int BACKOFF_SCHEDULE[] = {
    0,    /* 1 denial - no backoff */
    0,    /* 2 denials - no backoff */
    5,    /* 3 denials - 5 seconds */
    15,   /* 4 denials - 15 seconds */
    60,   /* 5 denials - 60 seconds */
    300   /* 6+ denials - 5 minutes */
};
#define BACKOFF_SCHEDULE_SIZE (sizeof(BACKOFF_SCHEDULE) / sizeof(BACKOFF_SCHEDULE[0]))

/* Initial capacities for dynamic arrays */
#define INITIAL_ALLOWLIST_CAPACITY 16
#define INITIAL_SHELL_ALLOWLIST_CAPACITY 16
#define INITIAL_DENIAL_TRACKER_CAPACITY 8

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
 * Parse a shell type string to ShellType enum.
 * Returns SHELL_TYPE_UNKNOWN if the string is not recognized.
 *
 * TODO: Remove this function when shell_parser.c is implemented and use
 * parse_shell_type() from shell_parser.h instead.
 */
static ShellType parse_shell_type_internal(const char *str) {
    if (str == NULL) {
        return SHELL_TYPE_UNKNOWN;
    }

    if (strcmp(str, "posix") == 0) {
        return SHELL_TYPE_POSIX;
    } else if (strcmp(str, "cmd") == 0) {
        return SHELL_TYPE_CMD;
    } else if (strcmp(str, "powershell") == 0) {
        return SHELL_TYPE_POWERSHELL;
    }

    return SHELL_TYPE_UNKNOWN;
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
                        shell_type = parse_shell_type_internal(shell_type_json->valuestring);
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
    if (result >= APPROVAL_ALLOWED && result <= APPROVAL_RATE_LIMITED) {
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

    /* Initialize denial trackers with initial capacity */
    config->denial_trackers = calloc(INITIAL_DENIAL_TRACKER_CAPACITY,
                                     sizeof(DenialTracker));
    if (config->denial_trackers == NULL) {
        free(config->shell_allowlist);
        free(config->allowlist);
        config->shell_allowlist = NULL;
        config->allowlist = NULL;
        return -1;
    }
    config->denial_tracker_capacity = INITIAL_DENIAL_TRACKER_CAPACITY;
    config->denial_tracker_count = 0;

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
     * (runtime "allow always" entries). This is tracked by static_allowlist_count. */
    int static_count = parent->static_allowlist_count;
    if (static_count > 0) {
        child->allowlist = calloc(static_count, sizeof(AllowlistEntry));
        if (child->allowlist == NULL) {
            return -1;
        }
        child->allowlist_capacity = static_count;
        child->allowlist_count = 0;

        /* Deep copy each static allowlist entry */
        for (int i = 0; i < static_count; i++) {
            AllowlistEntry *src = &parent->allowlist[i];
            AllowlistEntry *dst = &child->allowlist[i];

            dst->tool = src->tool ? strdup(src->tool) : NULL;
            dst->pattern = src->pattern ? strdup(src->pattern) : NULL;
            dst->valid = 0;

            /* Recompile the regex for the child */
            if (dst->pattern != NULL) {
                if (regcomp(&dst->compiled, dst->pattern, REG_EXTENDED | REG_NOSUB) == 0) {
                    dst->valid = 1;
                }
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

    /* Allocate and copy static shell allowlist entries from parent */
    int static_shell_count = parent->static_shell_allowlist_count;
    if (static_shell_count > 0) {
        child->shell_allowlist = calloc(static_shell_count, sizeof(ShellAllowEntry));
        if (child->shell_allowlist == NULL) {
            /* Clean up allowlist on failure */
            for (int i = 0; i < child->allowlist_count; i++) {
                free(child->allowlist[i].tool);
                free(child->allowlist[i].pattern);
                if (child->allowlist[i].valid) {
                    regfree(&child->allowlist[i].compiled);
                }
            }
            free(child->allowlist);
            child->allowlist = NULL;
            return -1;
        }
        child->shell_allowlist_capacity = static_shell_count;
        child->shell_allowlist_count = 0;

        /* Deep copy each static shell allowlist entry */
        for (int i = 0; i < static_shell_count; i++) {
            ShellAllowEntry *src = &parent->shell_allowlist[i];
            ShellAllowEntry *dst = &child->shell_allowlist[i];

            dst->shell_type = src->shell_type;
            dst->prefix_len = src->prefix_len;

            if (src->prefix_len > 0 && src->command_prefix != NULL) {
                dst->command_prefix = calloc(src->prefix_len, sizeof(char*));
                if (dst->command_prefix != NULL) {
                    for (int j = 0; j < src->prefix_len; j++) {
                        dst->command_prefix[j] = src->command_prefix[j] ?
                            strdup(src->command_prefix[j]) : NULL;
                    }
                }
            } else {
                dst->command_prefix = NULL;
            }

            child->shell_allowlist_count++;
        }
    } else {
        /* No static entries - allocate minimal capacity */
        child->shell_allowlist = calloc(INITIAL_SHELL_ALLOWLIST_CAPACITY, sizeof(ShellAllowEntry));
        if (child->shell_allowlist == NULL) {
            /* Clean up allowlist on failure */
            for (int i = 0; i < child->allowlist_count; i++) {
                free(child->allowlist[i].tool);
                free(child->allowlist[i].pattern);
                if (child->allowlist[i].valid) {
                    regfree(&child->allowlist[i].compiled);
                }
            }
            free(child->allowlist);
            child->allowlist = NULL;
            return -1;
        }
        child->shell_allowlist_capacity = INITIAL_SHELL_ALLOWLIST_CAPACITY;
        child->shell_allowlist_count = 0;
    }
    child->static_shell_allowlist_count = child->shell_allowlist_count;

    /* Initialize denial trackers - subagents start fresh */
    child->denial_trackers = calloc(INITIAL_DENIAL_TRACKER_CAPACITY, sizeof(DenialTracker));
    if (child->denial_trackers == NULL) {
        /* Clean up on failure */
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

        for (int i = 0; i < child->allowlist_count; i++) {
            free(child->allowlist[i].tool);
            free(child->allowlist[i].pattern);
            if (child->allowlist[i].valid) {
                regfree(&child->allowlist[i].compiled);
            }
        }
        free(child->allowlist);
        child->allowlist = NULL;
        return -1;
    }
    child->denial_tracker_capacity = INITIAL_DENIAL_TRACKER_CAPACITY;
    child->denial_tracker_count = 0;

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

static void free_denial_tracker(DenialTracker *tracker) {
    if (tracker == NULL) {
        return;
    }
    free(tracker->tool);
    tracker->tool = NULL;
}

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

    /* Free denial trackers */
    if (config->denial_trackers != NULL) {
        for (int i = 0; i < config->denial_tracker_count; i++) {
            free_denial_tracker(&config->denial_trackers[i]);
        }
        free(config->denial_trackers);
        config->denial_trackers = NULL;
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
    config->denial_tracker_count = 0;
    config->denial_tracker_capacity = 0;
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
 * Rate Limiting
 * ========================================================================== */

static DenialTracker *find_denial_tracker(const ApprovalGateConfig *config,
                                          const char *tool_name) {
    if (config == NULL || tool_name == NULL) {
        return NULL;
    }

    for (int i = 0; i < config->denial_tracker_count; i++) {
        if (config->denial_trackers[i].tool != NULL &&
            strcmp(config->denial_trackers[i].tool, tool_name) == 0) {
            return &config->denial_trackers[i];
        }
    }
    return NULL;
}

static DenialTracker *get_or_create_denial_tracker(ApprovalGateConfig *config,
                                                   const char *tool_name) {
    if (config == NULL || tool_name == NULL) {
        return NULL;
    }

    /* Check if tracker already exists */
    DenialTracker *tracker = find_denial_tracker(config, tool_name);
    if (tracker != NULL) {
        return tracker;
    }

    /* Need to create a new tracker */
    if (config->denial_tracker_count >= config->denial_tracker_capacity) {
        /* Grow the array */
        int new_capacity = config->denial_tracker_capacity * 2;
        DenialTracker *new_trackers = realloc(config->denial_trackers,
                                              new_capacity * sizeof(DenialTracker));
        if (new_trackers == NULL) {
            return NULL;
        }
        config->denial_trackers = new_trackers;
        config->denial_tracker_capacity = new_capacity;
    }

    /* Initialize new tracker */
    tracker = &config->denial_trackers[config->denial_tracker_count++];
    memset(tracker, 0, sizeof(*tracker));
    tracker->tool = strdup(tool_name);
    if (tracker->tool == NULL) {
        config->denial_tracker_count--;
        return NULL;
    }
    tracker->category = get_tool_category(tool_name);

    return tracker;
}

int is_rate_limited(const ApprovalGateConfig *config,
                    const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL || tool_call->name == NULL) {
        return 0;
    }

    const DenialTracker *tracker = find_denial_tracker(config, tool_call->name);
    if (tracker == NULL) {
        return 0;
    }

    time_t now = time(NULL);
    if (tracker->backoff_until > now) {
        return 1; /* Still in backoff period */
    }

    return 0;
}

void track_denial(ApprovalGateConfig *config, const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL || tool_call->name == NULL) {
        return;
    }

    DenialTracker *tracker = get_or_create_denial_tracker(config, tool_call->name);
    if (tracker == NULL) {
        return;
    }

    time_t now = time(NULL);
    tracker->denial_count++;
    tracker->last_denial = now;

    /* Calculate backoff based on denial count */
    int schedule_index = tracker->denial_count - 1;
    if (schedule_index < 0) {
        schedule_index = 0;
    }
    if (schedule_index >= (int)BACKOFF_SCHEDULE_SIZE) {
        schedule_index = BACKOFF_SCHEDULE_SIZE - 1;
    }

    int backoff_seconds = BACKOFF_SCHEDULE[schedule_index];
    tracker->backoff_until = now + backoff_seconds;
}

void reset_denial_tracker(ApprovalGateConfig *config, const char *tool) {
    if (config == NULL || tool == NULL) {
        return;
    }

    DenialTracker *tracker = find_denial_tracker(config, tool);
    if (tracker == NULL) {
        return;
    }

    tracker->denial_count = 0;
    tracker->last_denial = 0;
    tracker->backoff_until = 0;
}

int get_rate_limit_remaining(const ApprovalGateConfig *config, const char *tool) {
    if (config == NULL || tool == NULL) {
        return 0;
    }

    const DenialTracker *tracker = find_denial_tracker(config, tool);
    if (tracker == NULL) {
        return 0;
    }

    time_t now = time(NULL);
    if (tracker->backoff_until <= now) {
        return 0;
    }

    return (int)(tracker->backoff_until - now);
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
            if (commands_are_equivalent(entry->command_prefix[0], base_cmd,
                                        entry->shell_type, parsed->shell_type)) {
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

/* Signal handler for Ctrl+C during prompt */
static void prompt_sigint_handler(int sig) {
    (void)sig;
    g_prompt_interrupted = 1;
}

/**
 * Extract the shell command from tool call arguments.
 * Returns allocated string or NULL if not a shell command.
 */
static char *extract_shell_command(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->arguments == NULL) {
        return NULL;
    }

    if (tool_call->name == NULL || strcmp(tool_call->name, "shell") != 0) {
        return NULL;
    }

    cJSON *args = cJSON_Parse(tool_call->arguments);
    if (args == NULL) {
        return NULL;
    }

    cJSON *command_item = cJSON_GetObjectItem(args, "command");
    char *result = NULL;
    if (cJSON_IsString(command_item) && command_item->valuestring != NULL) {
        result = strdup(command_item->valuestring);
    }

    cJSON_Delete(args);
    return result;
}

/**
 * Extract file path from tool call arguments.
 * Returns allocated string or NULL if not available.
 */
static char *extract_file_path(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->arguments == NULL) {
        return NULL;
    }

    cJSON *args = cJSON_Parse(tool_call->arguments);
    if (args == NULL) {
        return NULL;
    }

    /* Try common path argument names */
    const char *path_keys[] = {"path", "file_path", "filepath", "filename", NULL};
    char *result = NULL;

    for (int i = 0; path_keys[i] != NULL; i++) {
        cJSON *path_item = cJSON_GetObjectItem(args, path_keys[i]);
        if (cJSON_IsString(path_item) && path_item->valuestring != NULL) {
            result = strdup(path_item->valuestring);
            break;
        }
    }

    cJSON_Delete(args);
    return result;
}

/**
 * Read a single keypress from the terminal in raw mode.
 * Returns the character read, or -1 on error/interrupt.
 *
 * Note: We deliberately do NOT set SA_RESTART on the signal handler
 * so that read() is interrupted by SIGINT, allowing us to detect Ctrl+C.
 */
static int read_single_keypress(void) {
    struct termios old_termios, new_termios;
    int ch = -1;
    int have_termios = 0;

    /* Set up Ctrl+C handler first (applies to both raw and cooked mode) */
    struct sigaction sa, old_sa;
    sa.sa_handler = prompt_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* No SA_RESTART - we want read() to be interrupted */
    sigaction(SIGINT, &sa, &old_sa);
    g_prompt_interrupted = 0;

    /* Get current terminal settings */
    if (tcgetattr(STDIN_FILENO, &old_termios) == 0) {
        /* Set up raw mode: disable canonical mode and echo */
        new_termios = old_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 1;   /* Wait for at least 1 character */
        new_termios.c_cc[VTIME] = 0;  /* No timeout */

        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
            have_termios = 1;
        }
    }

    /* Read single character (works in both raw and cooked mode) */
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1 && !g_prompt_interrupted) {
        ch = (unsigned char)c;
    }

    /* Restore terminal settings if we changed them */
    if (have_termios) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    }

    /* Restore signal handler */
    sigaction(SIGINT, &old_sa, NULL);

    /* Check if interrupted */
    if (g_prompt_interrupted) {
        return -1;
    }

    return ch;
}

/* Maximum width for content inside the details box (excluding borders) */
#define DETAILS_CONTENT_WIDTH 56

/**
 * Display the approval prompt details view.
 * Shows full arguments and resolved paths.
 */
static void display_details_view(const ToolCall *tool_call, const ApprovedPath *path) {
    fprintf(stderr, "\n");
    fprintf(stderr, "┌─ Details ────────────────────────────────────────────────────┐\n");

    /* Show tool name */
    fprintf(stderr, "│  Tool: %-53s │\n", tool_call->name ? tool_call->name : "unknown");
    fprintf(stderr, "│                                                              │\n");

    /* Show full arguments */
    fprintf(stderr, "│  Full arguments:                                             │\n");
    if (tool_call->arguments != NULL) {
        /* Pretty print JSON if possible */
        cJSON *json = cJSON_Parse(tool_call->arguments);
        if (json != NULL) {
            char *pretty = cJSON_Print(json);
            if (pretty != NULL) {
                /* Print each line with proper formatting, using strtok_r for thread safety */
                char *saveptr = NULL;
                char *line = strtok_r(pretty, "\n", &saveptr);
                while (line != NULL) {
                    /* Truncate lines that are too long */
                    size_t line_len = strlen(line);
                    if (line_len <= DETAILS_CONTENT_WIDTH) {
                        fprintf(stderr, "│    %-56s │\n", line);
                    } else {
                        /* Truncate with ellipsis */
                        fprintf(stderr, "│    %.53s... │\n", line);
                    }
                    line = strtok_r(NULL, "\n", &saveptr);
                }
                free(pretty);
            }
            cJSON_Delete(json);
        } else {
            /* Raw arguments (not valid JSON) - truncate if too long */
            size_t arg_len = strlen(tool_call->arguments);
            if (arg_len <= DETAILS_CONTENT_WIDTH) {
                fprintf(stderr, "│    %-56s │\n", tool_call->arguments);
            } else {
                fprintf(stderr, "│    %.53s... │\n", tool_call->arguments);
            }
        }
    } else {
        fprintf(stderr, "│    (none)                                                    │\n");
    }

    /* Show resolved path if available */
    if (path != NULL && path->resolved_path != NULL) {
        fprintf(stderr, "│                                                              │\n");
        fprintf(stderr, "│  Resolved path:                                              │\n");
        /* Truncate path if too long */
        char path_display[DETAILS_CONTENT_WIDTH + 1];
        size_t path_len = strlen(path->resolved_path);
        if (path_len <= DETAILS_CONTENT_WIDTH - 2) {
            snprintf(path_display, sizeof(path_display), "%s", path->resolved_path);
        } else {
            snprintf(path_display, sizeof(path_display), "...%s",
                     path->resolved_path + path_len - (DETAILS_CONTENT_WIDTH - 5));
        }
        fprintf(stderr, "│    %-56s │\n", path_display);

        if (path->existed) {
            fprintf(stderr, "│    (existing file)                                           │\n");
        } else {
            fprintf(stderr, "│    (new file)                                                │\n");
        }
    }

    fprintf(stderr, "└──────────────────────────────────────────────────────────────┘\n");
    fprintf(stderr, "\nPress any key to return to prompt...\n");

    /* Wait for keypress */
    read_single_keypress();
}

/* Maximum display width for prompt content */
#define PROMPT_CONTENT_WIDTH 50

ApprovalResult approval_gate_prompt(ApprovalGateConfig *config,
                                    const ToolCall *tool_call,
                                    ApprovedPath *out_path) {
    if (config == NULL || tool_call == NULL) {
        return APPROVAL_DENIED;
    }

    /* Check if we have a TTY available */
    if (!isatty(STDIN_FILENO)) {
        /* No TTY, default to deny for gated operations */
        return APPROVAL_DENIED;
    }

    /* Initialize out_path if provided */
    if (out_path != NULL) {
        memset(out_path, 0, sizeof(*out_path));
    }

    /* Extract display information based on tool type */
    char *shell_command = extract_shell_command(tool_call);
    char *file_path = extract_file_path(tool_call);
    GateCategory category = get_tool_category(tool_call->name);

    /* Iterative prompt loop to prevent stack overflow from repeated input */
    for (;;) {
        /* Display the approval prompt */
        fprintf(stderr, "\n");
        fprintf(stderr, "┌─ Approval Required ──────────────────────────────────────────┐\n");
        fprintf(stderr, "│                                                              │\n");
        fprintf(stderr, "│  Tool: %-53s │\n", tool_call->name ? tool_call->name : "unknown");

        /* Show command for shell, path for file tools, or args for others */
        if (shell_command != NULL) {
            char cmd_display[PROMPT_CONTENT_WIDTH + 4];
            size_t cmd_len = strlen(shell_command);
            if (cmd_len <= PROMPT_CONTENT_WIDTH) {
                snprintf(cmd_display, sizeof(cmd_display), "%s", shell_command);
            } else {
                snprintf(cmd_display, sizeof(cmd_display), "%.47s...", shell_command);
            }
            fprintf(stderr, "│  Command: %-50s │\n", cmd_display);
        } else if (file_path != NULL && (category == GATE_CATEGORY_FILE_READ ||
                                          category == GATE_CATEGORY_FILE_WRITE)) {
            char path_display[PROMPT_CONTENT_WIDTH + 4];
            size_t path_len = strlen(file_path);
            if (path_len <= PROMPT_CONTENT_WIDTH) {
                snprintf(path_display, sizeof(path_display), "%s", file_path);
            } else {
                /* Show end of path for long paths */
                snprintf(path_display, sizeof(path_display), "...%s",
                         file_path + path_len - 47);
            }
            fprintf(stderr, "│  Path: %-53s │\n", path_display);
        } else if (tool_call->arguments != NULL) {
            char arg_display[PROMPT_CONTENT_WIDTH + 4];
            size_t arg_len = strlen(tool_call->arguments);
            if (arg_len <= PROMPT_CONTENT_WIDTH) {
                snprintf(arg_display, sizeof(arg_display), "%s", tool_call->arguments);
            } else {
                snprintf(arg_display, sizeof(arg_display), "%.47s...", tool_call->arguments);
            }
            fprintf(stderr, "│  Args: %-53s │\n", arg_display);
        }

        fprintf(stderr, "│                                                              │\n");
        fprintf(stderr, "│  [y] Allow  [n] Deny  [a] Allow always  [?] Details          │\n");
        fprintf(stderr, "│                                                              │\n");
        fprintf(stderr, "└──────────────────────────────────────────────────────────────┘\n");
        fprintf(stderr, "> ");
        fflush(stderr);

        /* Read single keypress in raw mode */
        int response = read_single_keypress();
        fprintf(stderr, "\n");  /* Echo newline after keypress */

        /* Handle the response */
        if (response < 0) {
            /* Interrupted or error - clean up and abort */
            free(shell_command);
            free(file_path);
            return APPROVAL_ABORTED;
        }

        switch (tolower(response)) {
            case 'y':
                /* Reset denial tracker on approval */
                reset_denial_tracker(config, tool_call->name);
                free(shell_command);
                free(file_path);
                return APPROVAL_ALLOWED;

            case 'n':
                free(shell_command);
                free(file_path);
                return APPROVAL_DENIED;

            case 'a':
                /* Allow always - reset tracker and add to session allowlist */
                reset_denial_tracker(config, tool_call->name);
                /* Note: Pattern generation for persistent allowlist is handled by
                 * the caller when APPROVAL_ALLOWED_ALWAYS is returned */
                free(shell_command);
                free(file_path);
                return APPROVAL_ALLOWED_ALWAYS;

            case '?':
                /* Show details and continue loop to re-prompt */
                display_details_view(tool_call, out_path);
                continue;

            case 3: /* Ctrl+C */
            case 4: /* Ctrl+D */
                free(shell_command);
                free(file_path);
                return APPROVAL_ABORTED;

            default:
                /* Invalid input, continue loop to re-prompt */
                fprintf(stderr, "Invalid input. Press y, n, a, or ? for details.\n");
                continue;
        }
    }
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
                /* Not a subagent - prompt user directly via TTY */
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

/**
 * Format a summary line for a tool call (for batch display).
 * Returns allocated string that must be freed.
 */
static char *format_tool_summary(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->name == NULL) {
        return strdup("unknown");
    }

    char *summary = NULL;
    GateCategory category = get_tool_category(tool_call->name);

    /* Extract relevant info based on category */
    if (category == GATE_CATEGORY_SHELL) {
        char *cmd = extract_shell_command(tool_call);
        if (cmd != NULL) {
            /* Truncate long commands */
            if (strlen(cmd) > 40) {
                cmd[37] = '.';
                cmd[38] = '.';
                cmd[39] = '.';
                cmd[40] = '\0';
            }
            if (asprintf(&summary, "%s: %s", tool_call->name, cmd) < 0) {
                summary = NULL;
            }
            free(cmd);
        }
    } else if (category == GATE_CATEGORY_FILE_READ ||
               category == GATE_CATEGORY_FILE_WRITE) {
        char *path = extract_file_path(tool_call);
        if (path != NULL) {
            /* Truncate long paths */
            if (strlen(path) > 40) {
                /* Show end of path */
                char *truncated = path + strlen(path) - 37;
                if (asprintf(&summary, "%s: ...%s", tool_call->name, truncated) < 0) {
                    summary = NULL;
                }
            } else {
                if (asprintf(&summary, "%s: %s", tool_call->name, path) < 0) {
                    summary = NULL;
                }
            }
            free(path);
        }
    }

    /* Fallback to just tool name */
    if (summary == NULL) {
        summary = strdup(tool_call->name);
    }

    return summary;
}

/**
 * Display batch approval prompt.
 * Shows numbered list of operations.
 */
static void display_batch_prompt(const ToolCall *tool_calls, int count,
                                 const ApprovalResult *current_results) {
    fprintf(stderr, "\n");
    fprintf(stderr, "┌─ Approval Required (%d operations) ", count);
    /* Fill rest of header with dashes */
    int header_used = 28 + (count >= 10 ? 2 : 1);
    for (int i = header_used; i < 64; i++) {
        fprintf(stderr, "─");
    }
    fprintf(stderr, "┐\n");
    fprintf(stderr, "│                                                              │\n");

    /* List each operation */
    for (int i = 0; i < count; i++) {
        char *summary = format_tool_summary(&tool_calls[i]);
        char status_char = ' ';

        /* Show status indicator if already processed */
        if (current_results != NULL) {
            switch (current_results[i]) {
                case APPROVAL_ALLOWED:
                case APPROVAL_ALLOWED_ALWAYS:
                    status_char = '+';
                    break;
                case APPROVAL_DENIED:
                    status_char = '-';
                    break;
                default:
                    status_char = ' ';
                    break;
            }
        }

        /* Format: "│  1. [+] shell: ls -la                                         │" */
        char line[64];
        snprintf(line, sizeof(line), "%d. %s", i + 1, summary ? summary : "unknown");

        /* Truncate if too long */
        if (strlen(line) > 52) {
            line[49] = '.';
            line[50] = '.';
            line[51] = '.';
            line[52] = '\0';
        }

        if (status_char != ' ') {
            fprintf(stderr, "│  [%c] %-54s │\n", status_char, line);
        } else {
            fprintf(stderr, "│  %-58s │\n", line);
        }

        free(summary);
    }

    fprintf(stderr, "│                                                              │\n");
    if (count <= 9) {
        fprintf(stderr, "│  [y] Allow all  [n] Deny all  [1-%d] Review individual       │\n", count);
    } else {
        fprintf(stderr, "│  [y] Allow all  [n] Deny all  [1-%d] Review individual      │\n", count);
    }
    fprintf(stderr, "│                                                              │\n");
    fprintf(stderr, "└──────────────────────────────────────────────────────────────┘\n");
    fprintf(stderr, "> ");
    fflush(stderr);
}

/**
 * Read a number from terminal input (for selecting operation 1-N).
 * Returns the number read, or -1 on error/invalid input.
 */
static int read_operation_number(int first_digit, int max_value) {
    /* For single digit max, just return the first digit */
    if (max_value <= 9) {
        int num = first_digit - '0';
        if (num >= 1 && num <= max_value) {
            return num;
        }
        return -1;
    }

    /* For multi-digit, need to read more characters */
    char buf[16];
    buf[0] = (char)first_digit;
    int pos = 1;

    /* Read until we get a non-digit or buffer full */
    struct termios old_termios, new_termios;
    int have_termios = 0;

    if (tcgetattr(STDIN_FILENO, &old_termios) == 0) {
        new_termios = old_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 0;
        new_termios.c_cc[VTIME] = 2; /* 200ms timeout */
        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
            have_termios = 1;
        }
    }

    /* Read additional digits with timeout */
    while (pos < 15) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            break; /* Timeout or error */
        }
        if (c >= '0' && c <= '9') {
            buf[pos++] = c;
        } else if (c == '\n' || c == '\r') {
            break;
        } else {
            break;
        }
    }
    buf[pos] = '\0';

    if (have_termios) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    }

    /* Parse the number */
    char *endptr;
    long num = strtol(buf, &endptr, 10);
    if (*endptr != '\0' || num < 1 || num > max_value) {
        return -1;
    }

    return (int)num;
}

ApprovalResult approval_gate_prompt_batch(ApprovalGateConfig *config,
                                          const ToolCall *tool_calls,
                                          int count,
                                          ApprovalBatchResult *out_batch) {
    if (config == NULL || tool_calls == NULL || count <= 0 || out_batch == NULL) {
        return APPROVAL_DENIED;
    }

    /* Check if we have a TTY available */
    if (!isatty(STDIN_FILENO)) {
        /* No TTY, default to deny for gated operations */
        return APPROVAL_DENIED;
    }

    /* Initialize batch result */
    if (init_batch_result(out_batch, count) != 0) {
        return APPROVAL_DENIED;
    }

    /* Track which operations are pending */
    int *pending = calloc(count, sizeof(int));
    if (pending == NULL) {
        free_batch_result(out_batch);
        return APPROVAL_DENIED;
    }

    /* Initially all are pending */
    int pending_count = count;
    for (int i = 0; i < count; i++) {
        pending[i] = 1;
    }

    /* Interactive batch prompt loop */
    for (;;) {
        /* Display batch prompt with current status */
        display_batch_prompt(tool_calls, count,
                             pending_count < count ? out_batch->results : NULL);

        /* Read user input */
        int response = read_single_keypress();
        fprintf(stderr, "\n");

        if (response < 0) {
            /* Interrupted */
            free(pending);
            free_batch_result(out_batch);
            return APPROVAL_ABORTED;
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
                free(pending);
                return APPROVAL_ALLOWED;

            case 'n':
                /* Deny all pending operations */
                for (int i = 0; i < count; i++) {
                    if (pending[i]) {
                        out_batch->results[i] = APPROVAL_DENIED;
                    }
                }
                free(pending);
                return APPROVAL_DENIED;

            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
                {
                    /* Review individual operation */
                    int op_num = read_operation_number(response, count);
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
                        free(pending);
                        free_batch_result(out_batch);
                        return APPROVAL_ABORTED;
                    }

                    out_batch->results[idx] = single_result;
                    pending[idx] = 0;
                    pending_count--;

                    /* Check if all operations have been reviewed */
                    if (pending_count == 0) {
                        free(pending);
                        /* Return overall status */
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
                            return APPROVAL_DENIED;
                        }
                        if (all_always) {
                            return APPROVAL_ALLOWED_ALWAYS;
                        }
                        return APPROVAL_ALLOWED;
                    }

                    /* Continue with remaining operations */
                    continue;
                }

            case 3: /* Ctrl+C */
            case 4: /* Ctrl+D */
                free(pending);
                free_batch_result(out_batch);
                return APPROVAL_ABORTED;

            default:
                fprintf(stderr, "Invalid input. Press y, n, or 1-%d.\n", count);
                continue;
        }
    }
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
 * Allow Always Pattern Generation
 * ========================================================================== */

/**
 * Escape a string for use in a regex pattern.
 * Returns an allocated string that must be freed by the caller.
 */
static char *regex_escape(const char *str) {
    if (str == NULL) {
        return strdup("");
    }

    /* Characters that need escaping in POSIX extended regex */
    const char *meta = "\\^$.|?*+()[]{}";

    /* Calculate escaped length */
    size_t escaped_len = 0;
    for (const char *p = str; *p != '\0'; p++) {
        if (strchr(meta, *p) != NULL) {
            escaped_len += 2;  /* Backslash + char */
        } else {
            escaped_len += 1;
        }
    }

    char *result = malloc(escaped_len + 1);
    if (result == NULL) {
        return NULL;
    }

    char *out = result;
    for (const char *p = str; *p != '\0'; p++) {
        if (strchr(meta, *p) != NULL) {
            *out++ = '\\';
        }
        *out++ = *p;
    }
    *out = '\0';

    return result;
}

/**
 * Check if a path is in the root directory (no directory component other than ./).
 */
static int is_root_path(const char *path) {
    if (path == NULL) {
        return 0;
    }

    /* Skip leading ./ */
    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    }

    /* If there's no remaining slash, it's a root file */
    return strchr(path, '/') == NULL;
}

/**
 * Check if a path is in /tmp (security-sensitive, always exact match).
 */
static int is_tmp_path(const char *path) {
    if (path == NULL) {
        return 0;
    }
    return strncmp(path, "/tmp/", 5) == 0 || strncmp(path, "/tmp", 4) == 0;
}

/**
 * Extract the directory component of a path.
 * Returns allocated string or NULL on failure.
 */
static char *get_directory(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        return strdup(".");
    }

    size_t dir_len = last_slash - path;
    if (dir_len == 0) {
        return strdup("/");
    }

    char *dir = malloc(dir_len + 1);
    if (dir == NULL) {
        return NULL;
    }

    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';
    return dir;
}

/**
 * Extract the file extension (including the dot).
 * Returns pointer to the extension within the input string, or NULL if none.
 */
static const char *get_extension(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    const char *basename = last_slash ? last_slash + 1 : path;
    const char *last_dot = strrchr(basename, '.');

    /* No extension or hidden file (starts with dot) */
    if (last_dot == NULL || last_dot == basename) {
        return NULL;
    }

    return last_dot;
}

/**
 * Extract the basename of a path.
 * Returns pointer within the input string.
 */
static const char *get_basename_simple(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

void free_generated_pattern(GeneratedPattern *pattern) {
    if (pattern == NULL) {
        return;
    }

    free(pattern->pattern);
    pattern->pattern = NULL;

    if (pattern->command_prefix != NULL) {
        for (int i = 0; i < pattern->prefix_len; i++) {
            free(pattern->command_prefix[i]);
        }
        free(pattern->command_prefix);
        pattern->command_prefix = NULL;
    }
    pattern->prefix_len = 0;

    if (pattern->example_matches != NULL) {
        for (int i = 0; i < pattern->example_count; i++) {
            free(pattern->example_matches[i]);
        }
        free(pattern->example_matches);
        pattern->example_matches = NULL;
    }
    pattern->example_count = 0;
}

int generate_file_path_pattern(const char *path, GeneratedPattern *out_pattern) {
    if (path == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    /* Case 1: Root files get exact match */
    if (is_root_path(path)) {
        char *escaped = regex_escape(path);
        if (escaped == NULL) {
            return -1;
        }

        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);

        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Case 2: /tmp paths get exact match (security) */
    if (is_tmp_path(path)) {
        char *escaped = regex_escape(path);
        if (escaped == NULL) {
            return -1;
        }

        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);

        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Case 3: Regular paths - match directory and similar extensions */
    char *dir = get_directory(path);
    if (dir == NULL) {
        return -1;
    }

    char *escaped_dir = regex_escape(dir);
    free(dir);
    if (escaped_dir == NULL) {
        return -1;
    }

    const char *ext = get_extension(path);
    const char *basename = get_basename_simple(path);

    if (ext != NULL) {
        /* Has extension - match directory + any file with same extension */
        char *escaped_ext = regex_escape(ext);
        if (escaped_ext == NULL) {
            free(escaped_dir);
            return -1;
        }

        /* Check if basename has a prefix pattern (e.g., test_*.c) */
        const char *underscore = strchr(basename, '_');
        if (underscore != NULL && underscore < ext) {
            /* Has prefix pattern like "test_" - include it */
            size_t prefix_len = underscore - basename + 1;
            char *prefix = malloc(prefix_len + 1);
            if (prefix == NULL) {
                free(escaped_dir);
                free(escaped_ext);
                return -1;
            }
            memcpy(prefix, basename, prefix_len);
            prefix[prefix_len] = '\0';

            char *escaped_prefix = regex_escape(prefix);
            free(prefix);
            if (escaped_prefix == NULL) {
                free(escaped_dir);
                free(escaped_ext);
                return -1;
            }

            if (asprintf(&out_pattern->pattern, "^%s/%s.*%s$",
                         escaped_dir, escaped_prefix, escaped_ext) < 0) {
                free(escaped_dir);
                free(escaped_ext);
                free(escaped_prefix);
                return -1;
            }
            free(escaped_prefix);
        } else {
            /* No prefix pattern - match any file in directory with same extension */
            if (asprintf(&out_pattern->pattern, "^%s/.*%s$",
                         escaped_dir, escaped_ext) < 0) {
                free(escaped_dir);
                free(escaped_ext);
                return -1;
            }
        }
        free(escaped_ext);

        out_pattern->is_exact_match = 0;
        out_pattern->needs_confirmation = 1;

        /* Generate example matches (failures here are non-fatal) */
        out_pattern->example_matches = calloc(3, sizeof(char *));
        if (out_pattern->example_matches != NULL) {
            dir = get_directory(path);
            if (dir != NULL) {
                /* asprintf returns -1 on failure; glibc sets ptr to NULL but
                 * POSIX doesn't guarantee this, so we explicitly set on failure */
                if (asprintf(&out_pattern->example_matches[0], "%s/foo%s", dir, ext) < 0) {
                    out_pattern->example_matches[0] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[1], "%s/bar%s", dir, ext) < 0) {
                    out_pattern->example_matches[1] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[2], "%s/other%s", dir, ext) < 0) {
                    out_pattern->example_matches[2] = NULL;
                }
                out_pattern->example_count = 3;
                free(dir);
            }
        }
    } else {
        /* No extension - exact match only */
        char *escaped = regex_escape(path);
        free(escaped_dir);
        if (escaped == NULL) {
            return -1;
        }

        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);

        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    free(escaped_dir);
    return 0;
}

int generate_shell_command_pattern(const char *command, GeneratedPattern *out_pattern) {
    if (command == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    /* Parse the shell command */
    ParsedShellCommand *parsed = parse_shell_command(command);
    if (parsed == NULL) {
        return -1;
    }

    /* Commands with chain operators, pipes, subshells, redirects, or dangerous patterns
     * cannot have patterns generated - they require exact approval each time */
    if (!shell_command_is_safe_for_matching(parsed)) {
        /* Return empty pattern - caller should handle this as "no pattern possible" */
        free_parsed_shell_command(parsed);
        out_pattern->is_exact_match = 1;  /* Can only approve this exact command */
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Generate command prefix: base command + first argument */
    int prefix_len = parsed->token_count >= 2 ? 2 : parsed->token_count;
    if (prefix_len <= 0) {
        free_parsed_shell_command(parsed);
        return -1;
    }

    out_pattern->command_prefix = calloc(prefix_len, sizeof(char *));
    if (out_pattern->command_prefix == NULL) {
        free_parsed_shell_command(parsed);
        return -1;
    }

    for (int i = 0; i < prefix_len; i++) {
        out_pattern->command_prefix[i] = strdup(parsed->tokens[i]);
        if (out_pattern->command_prefix[i] == NULL) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                free(out_pattern->command_prefix[j]);
            }
            free(out_pattern->command_prefix);
            out_pattern->command_prefix = NULL;
            free_parsed_shell_command(parsed);
            return -1;
        }
    }
    out_pattern->prefix_len = prefix_len;

    /* If command has more than 2 tokens, pattern is broader than exact match */
    if (parsed->token_count > prefix_len) {
        out_pattern->is_exact_match = 0;
        out_pattern->needs_confirmation = 1;

        /* Generate example matches (failures here are non-fatal) */
        out_pattern->example_matches = calloc(3, sizeof(char *));
        if (out_pattern->example_matches != NULL) {
            if (prefix_len == 1) {
                if (asprintf(&out_pattern->example_matches[0], "%s --help", parsed->tokens[0]) < 0) {
                    out_pattern->example_matches[0] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[1], "%s -v", parsed->tokens[0]) < 0) {
                    out_pattern->example_matches[1] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[2], "%s <any args>", parsed->tokens[0]) < 0) {
                    out_pattern->example_matches[2] = NULL;
                }
            } else {
                if (asprintf(&out_pattern->example_matches[0], "%s %s <any args>",
                             parsed->tokens[0], parsed->tokens[1]) < 0) {
                    out_pattern->example_matches[0] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[1], "%s %s -v",
                             parsed->tokens[0], parsed->tokens[1]) < 0) {
                    out_pattern->example_matches[1] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[2], "%s %s --all",
                             parsed->tokens[0], parsed->tokens[1]) < 0) {
                    out_pattern->example_matches[2] = NULL;
                }
            }
            out_pattern->example_count = 3;
        }
    } else {
        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
    }

    free_parsed_shell_command(parsed);
    return 0;
}

int generate_network_url_pattern(const char *url, GeneratedPattern *out_pattern) {
    if (url == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    /* Parse URL: extract scheme and hostname */
    const char *scheme_end = strstr(url, "://");
    if (scheme_end == NULL) {
        /* Invalid URL format - return exact match */
        char *escaped = regex_escape(url);
        if (escaped == NULL) {
            return -1;
        }
        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);
        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Extract scheme */
    size_t scheme_len = scheme_end - url;
    char *scheme = malloc(scheme_len + 1);
    if (scheme == NULL) {
        return -1;
    }
    memcpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';

    /* Find hostname (ends at first / or : after ://) */
    const char *host_start = scheme_end + 3;
    const char *host_end = host_start;
    while (*host_end != '\0' && *host_end != '/' && *host_end != ':' && *host_end != '?') {
        host_end++;
    }

    size_t host_len = host_end - host_start;
    if (host_len == 0) {
        free(scheme);
        return -1;
    }

    char *hostname = malloc(host_len + 1);
    if (hostname == NULL) {
        free(scheme);
        return -1;
    }
    memcpy(hostname, host_start, host_len);
    hostname[host_len] = '\0';

    /* Escape scheme and hostname for regex */
    char *escaped_scheme = regex_escape(scheme);
    char *escaped_hostname = regex_escape(hostname);
    free(scheme);
    free(hostname);

    if (escaped_scheme == NULL || escaped_hostname == NULL) {
        free(escaped_scheme);
        free(escaped_hostname);
        return -1;
    }

    /* Generate pattern: ^scheme://hostname(/|$)
     * This requires either a path separator or end-of-string after hostname,
     * preventing subdomain spoofing like api.example.com.evil.com */
    if (asprintf(&out_pattern->pattern, "^%s://%s(/|$)",
                 escaped_scheme, escaped_hostname) < 0) {
        free(escaped_scheme);
        free(escaped_hostname);
        return -1;
    }

    free(escaped_scheme);
    free(escaped_hostname);

    out_pattern->is_exact_match = 0;
    out_pattern->needs_confirmation = 1;

    /* Generate example matches (failures here are non-fatal) */
    out_pattern->example_matches = calloc(3, sizeof(char *));
    if (out_pattern->example_matches != NULL) {
        /* Reconstruct base URL for examples */
        const char *base_end = host_end;
        size_t base_len = base_end - url;
        char *base_url = malloc(base_len + 1);
        if (base_url != NULL) {
            memcpy(base_url, url, base_len);
            base_url[base_len] = '\0';

            if (asprintf(&out_pattern->example_matches[0], "%s/any/path", base_url) < 0) {
                out_pattern->example_matches[0] = NULL;
            }
            if (asprintf(&out_pattern->example_matches[1], "%s/api/v1", base_url) < 0) {
                out_pattern->example_matches[1] = NULL;
            }
            if (asprintf(&out_pattern->example_matches[2], "%s", base_url) < 0) {
                out_pattern->example_matches[2] = NULL;
            }
            out_pattern->example_count = 3;
            free(base_url);
        }
    }

    return 0;
}

int generate_allowlist_pattern(const ToolCall *tool_call, GeneratedPattern *out_pattern) {
    if (tool_call == NULL || tool_call->name == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    GateCategory category = get_tool_category(tool_call->name);

    switch (category) {
        case GATE_CATEGORY_SHELL: {
            /* Extract command from arguments */
            if (tool_call->arguments == NULL) {
                return -1;
            }
            cJSON *args = cJSON_Parse(tool_call->arguments);
            if (args == NULL) {
                return -1;
            }
            cJSON *cmd_item = cJSON_GetObjectItem(args, "command");
            if (!cJSON_IsString(cmd_item) || cmd_item->valuestring == NULL) {
                cJSON_Delete(args);
                return -1;
            }
            int result = generate_shell_command_pattern(cmd_item->valuestring, out_pattern);
            cJSON_Delete(args);
            return result;
        }

        case GATE_CATEGORY_NETWORK: {
            /* Extract URL from arguments */
            if (tool_call->arguments == NULL) {
                return -1;
            }
            cJSON *args = cJSON_Parse(tool_call->arguments);
            if (args == NULL) {
                return -1;
            }
            cJSON *url_item = cJSON_GetObjectItem(args, "url");
            if (!cJSON_IsString(url_item) || url_item->valuestring == NULL) {
                cJSON_Delete(args);
                return -1;
            }
            int result = generate_network_url_pattern(url_item->valuestring, out_pattern);
            cJSON_Delete(args);
            return result;
        }

        case GATE_CATEGORY_FILE_WRITE:
        case GATE_CATEGORY_FILE_READ: {
            /* Extract path from arguments */
            if (tool_call->arguments == NULL) {
                return -1;
            }
            cJSON *args = cJSON_Parse(tool_call->arguments);
            if (args == NULL) {
                return -1;
            }
            /* Try common path argument names */
            cJSON *path_item = cJSON_GetObjectItem(args, "path");
            if (!cJSON_IsString(path_item)) {
                path_item = cJSON_GetObjectItem(args, "file_path");
            }
            if (!cJSON_IsString(path_item)) {
                path_item = cJSON_GetObjectItem(args, "filepath");
            }
            if (!cJSON_IsString(path_item) || path_item->valuestring == NULL) {
                cJSON_Delete(args);
                return -1;
            }
            int result = generate_file_path_pattern(path_item->valuestring, out_pattern);
            cJSON_Delete(args);
            return result;
        }

        default:
            /* For other categories, generate exact match on full arguments */
            if (tool_call->arguments != NULL) {
                char *escaped = regex_escape(tool_call->arguments);
                if (escaped == NULL) {
                    return -1;
                }
                if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
                    free(escaped);
                    return -1;
                }
                free(escaped);
            }
            out_pattern->is_exact_match = 1;
            out_pattern->needs_confirmation = 0;
            return 0;
    }
}

PatternConfirmResult confirm_pattern_scope(const GeneratedPattern *pattern,
                                           const char *original_value,
                                           char **out_edited) {
    if (pattern == NULL || original_value == NULL) {
        return PATTERN_CANCELLED;
    }

    /* If pattern doesn't need confirmation, just confirm it */
    if (!pattern->needs_confirmation) {
        return PATTERN_CONFIRMED;
    }

    /* Check if we have a TTY */
    if (!isatty(STDIN_FILENO)) {
        /* No TTY - can't get user input, use exact match */
        return PATTERN_EXACT_ONLY;
    }

    /* Display confirmation dialog */
    fprintf(stderr, "\n");
    fprintf(stderr, "┌─ Pattern Confirmation ───────────────────────────────────────┐\n");
    fprintf(stderr, "│                                                              │\n");
    fprintf(stderr, "│  This will allow future operations matching:                 │\n");

    if (pattern->pattern != NULL) {
        /* Regex pattern - truncate if too long */
        char pattern_display[52];
        size_t pattern_len = strlen(pattern->pattern);
        if (pattern_len <= 48) {
            snprintf(pattern_display, sizeof(pattern_display), "%s", pattern->pattern);
        } else {
            snprintf(pattern_display, sizeof(pattern_display), "%.45s...", pattern->pattern);
        }
        fprintf(stderr, "│  Pattern: %-50s │\n", pattern_display);
    } else if (pattern->command_prefix != NULL && pattern->prefix_len > 0) {
        /* Shell command prefix */
        char cmd_display[52];
        int pos = 0;
        for (int i = 0; i < pattern->prefix_len && pos < 48; i++) {
            if (i > 0) {
                cmd_display[pos++] = ' ';
            }
            size_t token_len = strlen(pattern->command_prefix[i]);
            if (pos + token_len < 48) {
                memcpy(cmd_display + pos, pattern->command_prefix[i], token_len);
                pos += token_len;
            }
        }
        cmd_display[pos] = '\0';
        fprintf(stderr, "│  Command: %-50s │\n", cmd_display);
    }

    fprintf(stderr, "│                                                              │\n");

    /* Show example matches */
    if (pattern->example_matches != NULL && pattern->example_count > 0) {
        fprintf(stderr, "│  Example matches:                                            │\n");
        for (int i = 0; i < pattern->example_count && i < 3; i++) {
            if (pattern->example_matches[i] != NULL) {
                char example_display[54];
                size_t ex_len = strlen(pattern->example_matches[i]);
                if (ex_len <= 50) {
                    snprintf(example_display, sizeof(example_display), "%s",
                             pattern->example_matches[i]);
                } else {
                    snprintf(example_display, sizeof(example_display), "%.47s...",
                             pattern->example_matches[i]);
                }
                fprintf(stderr, "│    %-54s │\n", example_display);
            }
        }
        fprintf(stderr, "│                                                              │\n");
    }

    fprintf(stderr, "│  [y] Confirm  [e] Edit pattern  [x] Exact match only         │\n");
    fprintf(stderr, "│                                                              │\n");
    fprintf(stderr, "└──────────────────────────────────────────────────────────────┘\n");
    fprintf(stderr, "> ");
    fflush(stderr);

    /* Read user response */
    int response = read_single_keypress();
    fprintf(stderr, "\n");

    if (response < 0) {
        return PATTERN_CANCELLED;
    }

    switch (tolower(response)) {
        case 'y':
            return PATTERN_CONFIRMED;

        case 'x':
            return PATTERN_EXACT_ONLY;

        case 'e':
            /* Read edited pattern from user */
            fprintf(stderr, "Enter pattern (Ctrl+C to cancel): ");
            fflush(stderr);

            char buf[1024];
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                return PATTERN_CANCELLED;
            }

            /* Remove trailing newline */
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }

            if (out_edited != NULL) {
                *out_edited = strdup(buf);
            }
            return PATTERN_EDITED;

        case 3: /* Ctrl+C */
        case 4: /* Ctrl+D */
            return PATTERN_CANCELLED;

        default:
            /* Invalid input - default to exact match */
            fprintf(stderr, "Invalid input. Using exact match.\n");
            return PATTERN_EXACT_ONLY;
    }
}

int apply_generated_pattern(ApprovalGateConfig *config,
                            const char *tool_name,
                            const GeneratedPattern *pattern) {
    if (config == NULL || tool_name == NULL || pattern == NULL) {
        return -1;
    }

    /* A GeneratedPattern should have either command_prefix (for shell) OR
     * pattern (for other tools), but not both. If both are set, prefer
     * command_prefix since that's more specific for shell commands. */
    int has_prefix = (pattern->command_prefix != NULL && pattern->prefix_len > 0);
    int has_regex = (pattern->pattern != NULL);

    /* If neither is set, this is an invalid/empty pattern */
    if (!has_prefix && !has_regex) {
        return -1;
    }

    /* For shell commands, add to shell allowlist */
    if (has_prefix) {
        return approval_gate_add_shell_allowlist(
            config,
            (const char **)pattern->command_prefix,
            pattern->prefix_len,
            SHELL_TYPE_UNKNOWN);
    }

    /* For other tools, add regex pattern */
    if (has_regex) {
        return approval_gate_add_allowlist(config, tool_name, pattern->pattern);
    }

    return -1;
}
