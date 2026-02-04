#include "allowlist.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include "util/darray.h"

typedef struct {
    char *tool;
    char *pattern;
    regex_t compiled;
    int valid;
} RegexEntry;

typedef struct {
    char **prefix;
    int prefix_len;
    ShellType shell_type;
} ShellEntry;

DARRAY_IMPL(RegexEntryArray, RegexEntry)
DARRAY_IMPL(ShellEntryArray, ShellEntry)

struct Allowlist {
    RegexEntryArray regex_entries;
    ShellEntryArray shell_entries;
};

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


Allowlist *allowlist_create(void) {
    Allowlist *al = calloc(1, sizeof(Allowlist));
    if (al == NULL) {
        return NULL;
    }

    if (RegexEntryArray_init(&al->regex_entries) != 0) {
        free(al);
        return NULL;
    }

    if (ShellEntryArray_init(&al->shell_entries) != 0) {
        RegexEntryArray_destroy(&al->regex_entries);
        free(al);
        return NULL;
    }

    return al;
}

void allowlist_destroy(Allowlist *al) {
    if (al == NULL) {
        return;
    }

    for (size_t i = 0; i < al->regex_entries.count; i++) {
        free_regex_entry(&al->regex_entries.data[i]);
    }
    RegexEntryArray_destroy(&al->regex_entries);

    for (size_t i = 0; i < al->shell_entries.count; i++) {
        free_shell_entry(&al->shell_entries.data[i]);
    }
    ShellEntryArray_destroy(&al->shell_entries);

    free(al);
}


int allowlist_add_regex(Allowlist *al, const char *tool, const char *pattern) {
    if (al == NULL || tool == NULL || pattern == NULL) {
        return -1;
    }

    RegexEntry entry = {0};

    entry.tool = strdup(tool);
    if (entry.tool == NULL) {
        return -1;
    }

    entry.pattern = strdup(pattern);
    if (entry.pattern == NULL) {
        free(entry.tool);
        return -1;
    }

    int ret = regcomp(&entry.compiled, pattern, REG_EXTENDED | REG_NOSUB);
    if (ret != 0) {
        free(entry.tool);
        free(entry.pattern);
        return -1;
    }
    entry.valid = 1;

    if (RegexEntryArray_push(&al->regex_entries, entry) != 0) {
        free_regex_entry(&entry);
        return -1;
    }

    return 0;
}

int allowlist_add_shell(Allowlist *al, const char **prefix, int prefix_len,
                        ShellType shell_type) {
    if (al == NULL || prefix == NULL || prefix_len <= 0) {
        return -1;
    }

    ShellEntry entry = {0};

    entry.prefix = calloc(prefix_len, sizeof(char *));
    if (entry.prefix == NULL) {
        return -1;
    }

    for (int i = 0; i < prefix_len; i++) {
        entry.prefix[i] = strdup(prefix[i]);
        if (entry.prefix[i] == NULL) {
            for (int j = 0; j < i; j++) {
                free(entry.prefix[j]);
            }
            free(entry.prefix);
            return -1;
        }
    }
    entry.prefix_len = prefix_len;
    entry.shell_type = shell_type;

    if (ShellEntryArray_push(&al->shell_entries, entry) != 0) {
        free_shell_entry(&entry);
        return -1;
    }

    return 0;
}


AllowlistMatchResult allowlist_check_regex(const Allowlist *al,
                                           const char *tool,
                                           const char *target) {
    if (al == NULL || tool == NULL || target == NULL) {
        return ALLOWLIST_NO_MATCH;
    }

    for (size_t i = 0; i < al->regex_entries.count; i++) {
        const RegexEntry *entry = &al->regex_entries.data[i];
        if (!entry->valid) {
            continue;
        }

        if (strcmp(entry->tool, tool) != 0) {
            continue;
        }

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

    for (size_t i = 0; i < al->shell_entries.count; i++) {
        const ShellEntry *entry = &al->shell_entries.data[i];

        if (entry->shell_type != SHELL_TYPE_UNKNOWN &&
            entry->shell_type != shell_type) {
            continue;
        }

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


int allowlist_regex_count(const Allowlist *al) {
    if (al == NULL) {
        return 0;
    }
    return (int)al->regex_entries.count;
}

int allowlist_shell_count(const Allowlist *al) {
    if (al == NULL) {
        return 0;
    }
    return (int)al->shell_entries.count;
}

void allowlist_clear_session(Allowlist *al, int keep_regex, int keep_shell) {
    if (al == NULL) {
        return;
    }

    while ((int)al->regex_entries.count > keep_regex) {
        al->regex_entries.count--;
        free_regex_entry(&al->regex_entries.data[al->regex_entries.count]);
    }

    while ((int)al->shell_entries.count > keep_shell) {
        al->shell_entries.count--;
        free_shell_entry(&al->shell_entries.data[al->shell_entries.count]);
    }
}
