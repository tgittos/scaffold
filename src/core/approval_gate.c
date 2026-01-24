/**
 * Approval Gates Implementation
 *
 * This module provides the core approval gate logic for requiring user
 * confirmation before executing potentially destructive operations.
 *
 * See SPEC_APPROVAL_GATES.md for the full specification.
 */

#include "approval_gate.h"

#include <cJSON.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

#ifdef _WIN32
/**
 * Case-insensitive substring search.
 * Returns pointer to first occurrence of needle in haystack, or NULL if not found.
 */
static const char *strcasestr_local(const char *haystack, const char *needle) {
    if (!haystack || !needle) {
        return NULL;
    }
    if (*needle == '\0') {
        return haystack;
    }

    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p != '\0'; p++) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return p;
        }
    }
    return NULL;
}
#endif /* _WIN32 */

#ifdef __linux__
/**
 * Get the last path component (basename), handling both / and \ separators.
 */
static const char *get_path_basename(const char *path) {
    if (!path) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    const char *last_backslash = strrchr(path, '\\');

    /* Use whichever separator appears last */
    const char *sep = NULL;
    if (last_slash && last_backslash) {
        sep = (last_slash > last_backslash) ? last_slash : last_backslash;
    } else if (last_slash) {
        sep = last_slash;
    } else if (last_backslash) {
        sep = last_backslash;
    }

    return sep ? sep + 1 : path;
}
#endif /* __linux__ */

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

/* Verify result messages */
static const char *VERIFY_MESSAGES[] = {
    [VERIFY_OK]                 = "Path verified successfully",
    [VERIFY_ERR_SYMLINK]        = "Path is a symbolic link",
    [VERIFY_ERR_DELETED]        = "File was deleted after approval",
    [VERIFY_ERR_OPEN]           = "Failed to open file",
    [VERIFY_ERR_STAT]           = "Failed to stat file",
    [VERIFY_ERR_INODE_MISMATCH] = "File changed since approval",
    [VERIFY_ERR_PARENT]         = "Cannot open parent directory",
    [VERIFY_ERR_PARENT_CHANGED] = "Parent directory changed since approval",
    [VERIFY_ERR_ALREADY_EXISTS] = "File already exists",
    [VERIFY_ERR_CREATE]         = "Failed to create file"
};

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
 */
static ShellType parse_shell_type(const char *str) {
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
                        shell_type = parse_shell_type(shell_type_json->valuestring);
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

const char *verify_result_message(VerifyResult result) {
    if (result >= VERIFY_OK && result <= VERIFY_ERR_CREATE) {
        return VERIFY_MESSAGES[result];
    }
    return "Unknown verification error";
}

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
 * Shell Detection
 * ========================================================================== */

ShellType detect_shell_type(void) {
#ifdef _WIN32
    /* On Windows, check COMSPEC and PSModulePath */
    const char *psmodule = getenv("PSModulePath");
    if (psmodule != NULL && strlen(psmodule) > 0) {
        return SHELL_TYPE_POWERSHELL;
    }

    const char *comspec = getenv("COMSPEC");
    if (comspec != NULL) {
        /* Check if cmd.exe (case-insensitive for all variations) */
        if (strcasestr_local(comspec, "cmd.exe") != NULL) {
            return SHELL_TYPE_CMD;
        }
    }
    return SHELL_TYPE_CMD; /* Default on Windows */
#else
    /* On POSIX, check SHELL for PowerShell variants */
    const char *shell = getenv("SHELL");
    if (shell != NULL) {
        if (strstr(shell, "pwsh") != NULL || strstr(shell, "powershell") != NULL) {
            return SHELL_TYPE_POWERSHELL;
        }
    }
    return SHELL_TYPE_POSIX;
#endif
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

int approval_gate_matches_allowlist(const ApprovalGateConfig *config,
                                    const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL || tool_call->name == NULL) {
        return 0;
    }

    GateCategory category = get_tool_category(tool_call->name);

    /* Shell commands use parsed command matching (not implemented here yet) */
    /* For now, we only support regex matching for non-shell tools */
    if (category == GATE_CATEGORY_SHELL) {
        /* TODO: Implement shell command prefix matching using shell_parser.h */
        /* For now, don't match any shell commands */
        return 0;
    }

    /* For other tools, use regex matching against the arguments */
    if (tool_call->arguments != NULL) {
        return match_regex_allowlist(config, tool_call->name, tool_call->arguments);
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

    /* Display the approval prompt */
    fprintf(stderr, "\n");
    fprintf(stderr, "┌─ Approval Required ──────────────────────────────────────────┐\n");
    fprintf(stderr, "│                                                              │\n");
    fprintf(stderr, "│  Tool: %-53s │\n", tool_call->name ? tool_call->name : "unknown");

    /* Show abbreviated arguments */
    if (tool_call->arguments != NULL) {
        char arg_display[54];
        size_t arg_len = strlen(tool_call->arguments);
        if (arg_len <= 50) {
            snprintf(arg_display, sizeof(arg_display), "%s", tool_call->arguments);
        } else {
            /* Use snprintf for safe truncation with ellipsis */
            snprintf(arg_display, sizeof(arg_display), "%.47s...", tool_call->arguments);
        }
        fprintf(stderr, "│  Args: %-53s │\n", arg_display);
    }

    fprintf(stderr, "│                                                              │\n");
    fprintf(stderr, "│  [y] Allow  [n] Deny  [a] Allow always  [?] Details          │\n");
    fprintf(stderr, "│                                                              │\n");
    fprintf(stderr, "└──────────────────────────────────────────────────────────────┘\n");
    fprintf(stderr, "> ");

    /* Read user input */
    char response = 0;
    ssize_t n = read(STDIN_FILENO, &response, 1);
    if (n <= 0) {
        return APPROVAL_ABORTED;
    }

    /* Handle the response */
    switch (tolower(response)) {
        case 'y':
            /* Reset denial tracker on approval */
            reset_denial_tracker(config, tool_call->name);

            /* Initialize out_path if provided (basic implementation) */
            if (out_path != NULL) {
                memset(out_path, 0, sizeof(*out_path));
            }
            return APPROVAL_ALLOWED;

        case 'n':
            return APPROVAL_DENIED;

        case 'a':
            /* Allow always - reset tracker and return */
            reset_denial_tracker(config, tool_call->name);

            /* TODO: Generate pattern and add to allowlist */
            /* For now, just allow this single operation */
            if (out_path != NULL) {
                memset(out_path, 0, sizeof(*out_path));
            }
            return APPROVAL_ALLOWED_ALWAYS;

        case '?':
            /* Show details and re-prompt */
            fprintf(stderr, "\nFull arguments:\n%s\n",
                   tool_call->arguments ? tool_call->arguments : "(none)");
            return approval_gate_prompt(config, tool_call, out_path);

        case 3: /* Ctrl+C */
        case 4: /* Ctrl+D */
            return APPROVAL_ABORTED;

        default:
            /* Invalid input, deny */
            return APPROVAL_DENIED;
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

/* =============================================================================
 * Path Verification (TOCTOU Protection)
 * ========================================================================== */

VerifyResult verify_approved_path(const ApprovedPath *approved) {
    if (approved == NULL || approved->resolved_path == NULL) {
        return VERIFY_ERR_OPEN;
    }

    struct stat st;

    if (approved->existed) {
        /* Existing file: verify inode/device match */
        if (stat(approved->resolved_path, &st) != 0) {
            if (errno == ENOENT) {
                return VERIFY_ERR_DELETED;
            }
            return VERIFY_ERR_STAT;
        }

        if (st.st_ino != approved->inode || st.st_dev != approved->device) {
            return VERIFY_ERR_INODE_MISMATCH;
        }
    } else {
        /* New file: verify parent directory */
        if (approved->parent_path == NULL) {
            return VERIFY_ERR_PARENT;
        }

        if (stat(approved->parent_path, &st) != 0) {
            return VERIFY_ERR_PARENT;
        }

        if (st.st_ino != approved->parent_inode ||
            st.st_dev != approved->parent_device) {
            return VERIFY_ERR_PARENT_CHANGED;
        }

        /* Check if file was created in the meantime */
        if (stat(approved->resolved_path, &st) == 0) {
            return VERIFY_ERR_ALREADY_EXISTS;
        }
    }

    return VERIFY_OK;
}

VerifyResult verify_and_open_approved_path(const ApprovedPath *approved,
                                           int flags,
                                           int *out_fd) {
    if (approved == NULL || out_fd == NULL) {
        return VERIFY_ERR_OPEN;
    }

    *out_fd = -1;

    if (approved->resolved_path == NULL) {
        return VERIFY_ERR_OPEN;
    }

    struct stat st;

    if (approved->existed) {
        /* Existing file: open with O_NOFOLLOW */
#ifdef O_NOFOLLOW
        int fd = open(approved->resolved_path, flags | O_NOFOLLOW);
#else
        int fd = open(approved->resolved_path, flags);
#endif
        if (fd < 0) {
            if (errno == ELOOP) {
                return VERIFY_ERR_SYMLINK;
            }
            if (errno == ENOENT) {
                return VERIFY_ERR_DELETED;
            }
            return VERIFY_ERR_OPEN;
        }

        /* Verify inode matches */
        if (fstat(fd, &st) != 0) {
            close(fd);
            return VERIFY_ERR_STAT;
        }

        if (st.st_ino != approved->inode || st.st_dev != approved->device) {
            close(fd);
            return VERIFY_ERR_INODE_MISMATCH;
        }

        *out_fd = fd;
        return VERIFY_OK;

    } else {
        /* New file: verify parent, create with O_EXCL */
        if (approved->parent_path == NULL) {
            return VERIFY_ERR_PARENT;
        }

#ifdef O_DIRECTORY
        int parent_fd = open(approved->parent_path, O_RDONLY | O_DIRECTORY);
#else
        int parent_fd = open(approved->parent_path, O_RDONLY);
#endif
        if (parent_fd < 0) {
            return VERIFY_ERR_PARENT;
        }

        /* Verify parent inode */
        if (fstat(parent_fd, &st) != 0 ||
            st.st_ino != approved->parent_inode ||
            st.st_dev != approved->parent_device) {
            close(parent_fd);
            return VERIFY_ERR_PARENT_CHANGED;
        }

        /* Create file with O_EXCL */
#ifdef O_NOFOLLOW
        int create_flags = flags | O_CREAT | O_EXCL | O_NOFOLLOW;
#else
        int create_flags = flags | O_CREAT | O_EXCL;
#endif

        int fd;
#ifdef __linux__
        /* Extract basename (handles both / and \ path separators) */
        const char *basename = get_path_basename(approved->user_path);
        fd = openat(parent_fd, basename, create_flags, 0644);
#else
        /* Fallback for systems without openat */
        fd = open(approved->resolved_path, create_flags, 0644);
#endif

        /* Always close parent_fd after file creation attempt */
        close(parent_fd);

        if (fd < 0) {
            if (errno == EEXIST) {
                return VERIFY_ERR_ALREADY_EXISTS;
            }
            return VERIFY_ERR_CREATE;
        }

        *out_fd = fd;
        return VERIFY_OK;
    }
}

void free_approved_path(ApprovedPath *path) {
    if (path == NULL) {
        return;
    }

    free(path->user_path);
    free(path->resolved_path);
    free(path->parent_path);

    path->user_path = NULL;
    path->resolved_path = NULL;
    path->parent_path = NULL;
}

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

char *format_verify_error(VerifyResult result, const char *path) {
    if (path == NULL) {
        path = "unknown";
    }

    const char *message = verify_result_message(result);

    /* Escape path for JSON (message is from static strings, no escaping needed) */
    char *escaped_path = json_escape_string_simple(path);
    if (escaped_path == NULL) {
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"path_changed\", "
                       "\"message\": \"%s\", "
                       "\"path\": \"%s\"}",
                       message, escaped_path);

    free(escaped_path);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

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
