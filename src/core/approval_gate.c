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

    return 0;
}

int approval_gate_init_from_parent(ApprovalGateConfig *child,
                                   const ApprovalGateConfig *parent) {
    if (child == NULL || parent == NULL) {
        return -1;
    }

    /* Initialize child with defaults first */
    if (approval_gate_init(child) != 0) {
        return -1;
    }

    /* Copy parent's enabled state and category configuration */
    child->enabled = parent->enabled;
    for (int i = 0; i < GATE_CATEGORY_COUNT; i++) {
        child->categories[i] = parent->categories[i];
    }

    /* Note: Static allowlist (from config file) is inherited.
     * Session allowlist (runtime "allow always" entries) is NOT inherited.
     * Currently both types share the same array, but session entries are only
     * added by the root process via approval_gate_prompt(). Since subagents
     * start fresh with approval_gate_init_from_parent(), they won't have any
     * session entries from the parent. */

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
            /* Requires approval - prompt user */
            return approval_gate_prompt(config, tool_call, out_path);

        default:
            return APPROVAL_DENIED;
    }
}

/* Path verification functions (verify_approved_path, verify_and_open_approved_path,
 * free_approved_path) are implemented in atomic_file.c */

/* =============================================================================
 * Subagent Approval Proxy
 * ========================================================================== */

ApprovalResult subagent_request_approval(const ApprovalChannel *channel,
                                         const ToolCall *tool_call,
                                         ApprovedPath *out_path) {
    /* TODO: Implement IPC-based approval proxying */
    /* For now, subagents without a channel deny all gated operations */
    (void)channel;
    (void)tool_call;
    (void)out_path;
    return APPROVAL_DENIED;
}

void handle_subagent_approval_request(ApprovalGateConfig *config,
                                      ApprovalChannel *channel) {
    /* TODO: Implement parent-side approval request handling */
    (void)config;
    (void)channel;
}

void free_approval_channel(ApprovalChannel *channel) {
    if (channel == NULL) {
        return;
    }

    if (channel->request_fd >= 0) {
        close(channel->request_fd);
    }
    if (channel->response_fd >= 0) {
        close(channel->response_fd);
    }

    free(channel);
}

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
