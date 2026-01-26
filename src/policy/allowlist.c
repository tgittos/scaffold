/**
 * Allowlist Implementation
 *
 * Manages approval allowlists for the gate system.
 * This is an opaque type that owns regex and shell entries.
 */

#include "allowlist.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * Internal Data Structures
 * ========================================================================== */

/**
 * Regex-based allowlist entry.
 */
typedef struct {
    char *tool;             /* Tool name (exact match) */
    char *pattern;          /* Original pattern string */
    regex_t compiled;       /* Pre-compiled regex */
    int valid;              /* 1 if regex compiled successfully */
} RegexEntry;

/**
 * Shell command prefix entry.
 */
typedef struct {
    char **prefix;          /* Command tokens */
    int prefix_len;         /* Number of tokens */
    ShellType shell_type;   /* Shell type to match, or UNKNOWN for any */
} ShellEntry;

/**
 * Allowlist internal structure.
 */
struct Allowlist {
    /* Regex entries */
    RegexEntry *regex_entries;
    int regex_count;
    int regex_capacity;

    /* Shell entries */
    ShellEntry *shell_entries;
    int shell_count;
    int shell_capacity;
};

/* =============================================================================
 * Constants
 * ========================================================================== */

#define INITIAL_CAPACITY 16

/* =============================================================================
 * Internal Helper Functions
 * ========================================================================== */

/**
 * Free a regex entry's resources.
 */
static void free_regex_entry(RegexEntry *entry) {
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

/**
 * Free a shell entry's resources.
 */
static void free_shell_entry(ShellEntry *entry) {
    if (entry == NULL) {
        return;
    }
    if (entry->prefix != NULL) {
        for (int i = 0; i < entry->prefix_len; i++) {
            free(entry->prefix[i]);
        }
        free(entry->prefix);
    }
    entry->prefix = NULL;
    entry->prefix_len = 0;
}

/* =============================================================================
 * Public API - Lifecycle
 * ========================================================================== */

Allowlist *allowlist_create(void) {
    Allowlist *al = calloc(1, sizeof(Allowlist));
    if (al == NULL) {
        return NULL;
    }

    al->regex_entries = calloc(INITIAL_CAPACITY, sizeof(RegexEntry));
    if (al->regex_entries == NULL) {
        free(al);
        return NULL;
    }
    al->regex_capacity = INITIAL_CAPACITY;

    al->shell_entries = calloc(INITIAL_CAPACITY, sizeof(ShellEntry));
    if (al->shell_entries == NULL) {
        free(al->regex_entries);
        free(al);
        return NULL;
    }
    al->shell_capacity = INITIAL_CAPACITY;

    return al;
}

void allowlist_destroy(Allowlist *al) {
    if (al == NULL) {
        return;
    }

    /* Free regex entries */
    if (al->regex_entries != NULL) {
        for (int i = 0; i < al->regex_count; i++) {
            free_regex_entry(&al->regex_entries[i]);
        }
        free(al->regex_entries);
    }

    /* Free shell entries */
    if (al->shell_entries != NULL) {
        for (int i = 0; i < al->shell_count; i++) {
            free_shell_entry(&al->shell_entries[i]);
        }
        free(al->shell_entries);
    }

    free(al);
}

/* =============================================================================
 * Public API - Adding Entries
 * ========================================================================== */

int allowlist_add_regex(Allowlist *al, const char *tool, const char *pattern) {
    if (al == NULL || tool == NULL || pattern == NULL) {
        return -1;
    }

    /* Grow array if needed */
    if (al->regex_count >= al->regex_capacity) {
        int new_capacity = al->regex_capacity * 2;
        RegexEntry *new_entries = realloc(al->regex_entries,
                                          new_capacity * sizeof(RegexEntry));
        if (new_entries == NULL) {
            return -1;
        }
        al->regex_entries = new_entries;
        al->regex_capacity = new_capacity;
    }

    RegexEntry *entry = &al->regex_entries[al->regex_count];
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
        free(entry->tool);
        free(entry->pattern);
        entry->tool = NULL;
        entry->pattern = NULL;
        return -1;
    }
    entry->valid = 1;

    al->regex_count++;
    return 0;
}

int allowlist_add_shell(Allowlist *al, const char **prefix, int prefix_len,
                        ShellType shell_type) {
    if (al == NULL || prefix == NULL || prefix_len <= 0) {
        return -1;
    }

    /* Grow array if needed */
    if (al->shell_count >= al->shell_capacity) {
        int new_capacity = al->shell_capacity * 2;
        ShellEntry *new_entries = realloc(al->shell_entries,
                                          new_capacity * sizeof(ShellEntry));
        if (new_entries == NULL) {
            return -1;
        }
        al->shell_entries = new_entries;
        al->shell_capacity = new_capacity;
    }

    ShellEntry *entry = &al->shell_entries[al->shell_count];
    memset(entry, 0, sizeof(*entry));

    entry->prefix = calloc(prefix_len, sizeof(char *));
    if (entry->prefix == NULL) {
        return -1;
    }

    for (int i = 0; i < prefix_len; i++) {
        entry->prefix[i] = strdup(prefix[i]);
        if (entry->prefix[i] == NULL) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                free(entry->prefix[j]);
            }
            free(entry->prefix);
            entry->prefix = NULL;
            return -1;
        }
    }
    entry->prefix_len = prefix_len;
    entry->shell_type = shell_type;

    al->shell_count++;
    return 0;
}

/* =============================================================================
 * Public API - Checking Entries
 * ========================================================================== */

AllowlistMatchResult allowlist_check_regex(const Allowlist *al,
                                           const char *tool,
                                           const char *target) {
    if (al == NULL || tool == NULL || target == NULL) {
        return ALLOWLIST_NO_MATCH;
    }

    for (int i = 0; i < al->regex_count; i++) {
        const RegexEntry *entry = &al->regex_entries[i];
        if (!entry->valid) {
            continue;
        }

        /* Tool must match exactly */
        if (strcmp(entry->tool, tool) != 0) {
            continue;
        }

        /* Check regex match */
        if (regexec(&entry->compiled, target, 0, NULL, 0) == 0) {
            return ALLOWLIST_MATCHED;
        }
    }

    return ALLOWLIST_NO_MATCH;
}

AllowlistMatchResult allowlist_check_shell(const Allowlist *al,
                                           const char **tokens,
                                           int token_count,
                                           ShellType shell_type) {
    if (al == NULL || tokens == NULL || token_count <= 0) {
        return ALLOWLIST_NO_MATCH;
    }

    for (int i = 0; i < al->shell_count; i++) {
        const ShellEntry *entry = &al->shell_entries[i];

        /* Check shell type if specified */
        if (entry->shell_type != SHELL_TYPE_UNKNOWN &&
            entry->shell_type != shell_type) {
            continue;
        }

        /* Check if command starts with the prefix */
        if (token_count < entry->prefix_len) {
            continue;
        }

        int match = 1;
        for (int j = 0; j < entry->prefix_len; j++) {
            if (strcmp(tokens[j], entry->prefix[j]) != 0) {
                match = 0;
                break;
            }
        }

        if (match) {
            return ALLOWLIST_MATCHED;
        }
    }

    return ALLOWLIST_NO_MATCH;
}

/* =============================================================================
 * Public API - Counts and Management
 * ========================================================================== */

int allowlist_regex_count(const Allowlist *al) {
    if (al == NULL) {
        return 0;
    }
    return al->regex_count;
}

int allowlist_shell_count(const Allowlist *al) {
    if (al == NULL) {
        return 0;
    }
    return al->shell_count;
}

void allowlist_clear_session(Allowlist *al, int keep_regex, int keep_shell) {
    if (al == NULL) {
        return;
    }

    /* Clear regex entries beyond keep_regex */
    while (al->regex_count > keep_regex) {
        al->regex_count--;
        free_regex_entry(&al->regex_entries[al->regex_count]);
    }

    /* Clear shell entries beyond keep_shell */
    while (al->shell_count > keep_shell) {
        al->shell_count--;
        free_shell_entry(&al->shell_entries[al->shell_count]);
    }
}
