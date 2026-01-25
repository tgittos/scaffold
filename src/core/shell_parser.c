/**
 * Cross-Platform Shell Command Parser Implementation
 *
 * Parses shell commands to detect dangerous patterns, command chaining,
 * and to enable secure allowlist matching.
 *
 * This implementation focuses on POSIX shell parsing. Windows cmd.exe and
 * PowerShell parsing will be implemented in separate files.
 */

#include "shell_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal Constants
 * ========================================================================== */

/* Initial capacity for token array */
#define INITIAL_TOKEN_CAPACITY 16

/* Maximum command length we'll process */
#define MAX_COMMAND_LENGTH 65536

/* ============================================================================
 * Dangerous Pattern Definitions
 * ========================================================================== */

/**
 * Dangerous command patterns that always require approval.
 * These are checked against the raw command string before parsing.
 */
static const char *DANGEROUS_PATTERNS[] = {
    "rm -rf",
    "rm -fr",
    "rm -r -f",
    "rm -f -r",
    "chmod 777",
    "chmod -R",
    ":(){ :|:& };:",  /* Fork bomb */
    NULL
};

/**
 * Patterns that indicate remote code execution attempts.
 * These combine download + execution.
 */
static const char *RCE_DOWNLOAD_CMDS[] = {
    "curl",
    "wget",
    NULL
};

static const char *RCE_SHELL_CMDS[] = {
    "| sh",
    "| bash",
    "| zsh",
    "|sh",
    "|bash",
    "|zsh",
    NULL
};

/**
 * Patterns for disk write attacks.
 */
static const char *DISK_WRITE_PATTERNS[] = {
    "of=/dev/sd",
    "of=/dev/hd",
    "of=/dev/nvme",
    "> /dev/sd",
    "> /dev/hd",
    "> /dev/nvme",
    NULL
};

/* ============================================================================
 * Shell Type Detection
 * ========================================================================== */

ShellType detect_shell_type(void) {
#ifdef _WIN32
    /* Windows: Check environment to distinguish cmd.exe vs PowerShell */
    const char *psmodule = getenv("PSModulePath");
    const char *comspec = getenv("COMSPEC");

    /* PSModulePath is set when running in PowerShell */
    if (psmodule && strlen(psmodule) > 0) {
        return SHELL_TYPE_POWERSHELL;
    }

    /* Check COMSPEC for cmd.exe */
    if (comspec) {
        /* Case-insensitive search for cmd.exe */
        const char *p = comspec;
        while (*p) {
            if ((p[0] == 'c' || p[0] == 'C') &&
                (p[1] == 'm' || p[1] == 'M') &&
                (p[2] == 'd' || p[2] == 'D') &&
                (p[3] == '.' || p[3] == '\0')) {
                return SHELL_TYPE_CMD;
            }
            p++;
        }
    }

    /* Default to cmd on Windows */
    return SHELL_TYPE_CMD;
#else
    /* POSIX: Check SHELL environment variable */
    const char *shell = getenv("SHELL");
    if (shell) {
        /* Check for PowerShell Core */
        if (strstr(shell, "pwsh") || strstr(shell, "powershell")) {
            return SHELL_TYPE_POWERSHELL;
        }
    }

    /* Default to POSIX on non-Windows */
    return SHELL_TYPE_POSIX;
#endif
}

const char *shell_type_name(ShellType type) {
    switch (type) {
        case SHELL_TYPE_POSIX:
            return "posix";
        case SHELL_TYPE_CMD:
            return "cmd";
        case SHELL_TYPE_POWERSHELL:
            return "powershell";
        case SHELL_TYPE_UNKNOWN:
        default:
            return "unknown";
    }
}

int parse_shell_type(const char *name, ShellType *out_type) {
    if (!name || !out_type) {
        return -1;
    }

    /* Case-insensitive comparison */
    if (strcasecmp(name, "posix") == 0 ||
        strcasecmp(name, "bash") == 0 ||
        strcasecmp(name, "sh") == 0 ||
        strcasecmp(name, "zsh") == 0 ||
        strcasecmp(name, "dash") == 0) {
        *out_type = SHELL_TYPE_POSIX;
        return 0;
    }

    if (strcasecmp(name, "cmd") == 0 ||
        strcasecmp(name, "cmd.exe") == 0) {
        *out_type = SHELL_TYPE_CMD;
        return 0;
    }

    if (strcasecmp(name, "powershell") == 0 ||
        strcasecmp(name, "pwsh") == 0 ||
        strcasecmp(name, "ps") == 0) {
        *out_type = SHELL_TYPE_POWERSHELL;
        return 0;
    }

    return -1;
}

/* ============================================================================
 * Dangerous Pattern Detection
 * ========================================================================== */

int shell_command_is_dangerous(const char *command) {
    if (!command || !*command) {
        return 0;
    }

    /* Check direct dangerous patterns */
    for (int i = 0; DANGEROUS_PATTERNS[i]; i++) {
        if (strstr(command, DANGEROUS_PATTERNS[i])) {
            return 1;
        }
    }

    /* Check for remote code execution (download + pipe to shell) */
    int has_download = 0;
    for (int i = 0; RCE_DOWNLOAD_CMDS[i]; i++) {
        if (strstr(command, RCE_DOWNLOAD_CMDS[i])) {
            has_download = 1;
            break;
        }
    }

    if (has_download) {
        for (int i = 0; RCE_SHELL_CMDS[i]; i++) {
            if (strstr(command, RCE_SHELL_CMDS[i])) {
                return 1;
            }
        }
    }

    /* Check for disk write attacks */
    for (int i = 0; DISK_WRITE_PATTERNS[i]; i++) {
        if (strstr(command, DISK_WRITE_PATTERNS[i])) {
            return 1;
        }
    }

    /* Check for dd command with device output */
    if (strstr(command, "dd ") && strstr(command, "of=/dev/")) {
        return 1;
    }

    return 0;
}

int powershell_command_is_dangerous(const char *command) {
    if (!command || !*command) {
        return 0;
    }

    /* Dangerous PowerShell cmdlets and patterns (case-insensitive) */
    static const char *PS_DANGEROUS[] = {
        "invoke-expression",
        "invoke-command",
        "start-process",
        "invoke-webrequest",
        "invoke-restmethod",
        "iex",
        "icm",
        "iwr",
        "irm",
        "-encodedcommand",
        "-enc",
        "downloadstring",
        "downloadfile",
        NULL
    };

    /* Create lowercase copy for case-insensitive search */
    size_t len = strlen(command);
    char *lower = malloc(len + 1);
    if (!lower) {
        return 0;
    }

    for (size_t i = 0; i <= len; i++) {
        lower[i] = (char)tolower((unsigned char)command[i]);
    }

    int is_dangerous = 0;
    for (int i = 0; PS_DANGEROUS[i]; i++) {
        if (strstr(lower, PS_DANGEROUS[i])) {
            is_dangerous = 1;
            break;
        }
    }

    free(lower);
    return is_dangerous;
}

/* ============================================================================
 * POSIX Shell Parsing Helpers
 * ========================================================================== */

/**
 * Token accumulator for building parsed results.
 */
typedef struct {
    char **tokens;
    int count;
    int capacity;
} TokenList;

static int token_list_init(TokenList *list) {
    list->tokens = malloc(INITIAL_TOKEN_CAPACITY * sizeof(char *));
    if (!list->tokens) {
        return -1;
    }
    list->count = 0;
    list->capacity = INITIAL_TOKEN_CAPACITY;
    return 0;
}

static int token_list_add(TokenList *list, const char *start, size_t len) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity * 2;
        if (new_cap < list->capacity) {  /* Overflow check */
            return -1;
        }
        char **new_tokens = realloc(list->tokens, (size_t)new_cap * sizeof(char *));
        if (!new_tokens) {
            return -1;
        }
        list->tokens = new_tokens;
        list->capacity = new_cap;
    }

    char *token = malloc(len + 1);
    if (!token) {
        return -1;
    }
    memcpy(token, start, len);
    token[len] = '\0';

    list->tokens[list->count++] = token;
    return 0;
}

static void token_list_free(TokenList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->tokens[i]);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

/**
 * Check if a character is a POSIX shell metacharacter.
 */
static int is_posix_metachar(char c) {
    return c == ';' || c == '|' || c == '&' || c == '(' || c == ')' ||
           c == '$' || c == '`' || c == '>' || c == '<';
}

/**
 * Parse POSIX shell command into tokens and detect metacharacters.
 */
int parse_posix_shell(const char *command, ParsedShellCommand *result) {
    if (!command || !result) {
        return -1;
    }

    /* Initialize result */
    result->tokens = NULL;
    result->token_count = 0;
    result->has_chain = 0;
    result->has_pipe = 0;
    result->has_subshell = 0;
    result->has_redirect = 0;
    result->is_dangerous = shell_command_is_dangerous(command);
    result->shell_type = SHELL_TYPE_POSIX;

    TokenList tokens;
    if (token_list_init(&tokens) != 0) {
        return -1;
    }

    /* Current token being built */
    char *token_buf = malloc(strlen(command) + 1);
    if (!token_buf) {
        token_list_free(&tokens);
        return -1;
    }
    size_t token_len = 0;

    const char *p = command;
    int in_single_quote = 0;
    int in_double_quote = 0;
    int had_quotes = 0;  /* Track if we've seen quotes for current token */

    while (*p) {
        char c = *p;

        /*
         * Security: Check for non-ASCII characters.
         * Unicode lookalikes for operators (e.g., U+037E Greek Question Mark
         * looks like semicolon) could bypass detection. Flag as potentially
         * dangerous by setting has_chain.
         */
        if ((unsigned char)c > 127) {
            result->has_chain = 1;
        }

        /*
         * Security: Detect ANSI-C quoting ($'...')
         * This allows encoding metacharacters like $'\x3b' for semicolon.
         * Must check before entering single quote mode.
         */
        if (c == '$' && *(p + 1) == '\'') {
            result->has_chain = 1;  /* ANSI-C quoting can hide metacharacters */
            p++;  /* Skip the $ */
            continue;
        }

        /*
         * Security: Handle backslash escapes outside quotes.
         * In POSIX shells, backslash can escape any character including
         * metacharacters. A backslash at end of line is line continuation.
         * Mark as unsafe for matching rather than trying to parse all cases.
         */
        if (c == '\\' && !in_single_quote) {
            result->has_chain = 1;  /* Backslash escapes are complex; mark unsafe */
            if (*(p + 1)) {
                p += 2;  /* Skip backslash and escaped character */
            } else {
                p++;  /* Just skip backslash at end */
            }
            continue;
        }

        /* Handle quotes */
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            had_quotes = 1;  /* Mark that this token had quotes */
            p++;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            had_quotes = 1;  /* Mark that this token had quotes */
            p++;
            continue;
        }

        /* Inside quotes, just accumulate characters */
        if (in_single_quote || in_double_quote) {
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        /* Outside quotes - check for whitespace */
        if (isspace((unsigned char)c)) {
            /* End current token if any (or if we had empty quotes) */
            if (token_len > 0 || had_quotes) {
                if (token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
                had_quotes = 0;
            }
            p++;
            continue;
        }

        /* Check for metacharacters */
        if (is_posix_metachar(c)) {
            /* Detect specific patterns */
            if (c == ';') {
                result->has_chain = 1;
            } else if (c == '|') {
                /* Check for || */
                if (*(p + 1) == '|') {
                    result->has_chain = 1;
                } else {
                    result->has_pipe = 1;
                }
            } else if (c == '&') {
                /* Check for && */
                if (*(p + 1) == '&') {
                    result->has_chain = 1;
                }
                /* Single & is background, which we'll treat as chain for safety */
                else {
                    result->has_chain = 1;
                }
            } else if (c == '$') {
                /* Check for $( */
                if (*(p + 1) == '(') {
                    result->has_subshell = 1;
                }
            } else if (c == '`') {
                result->has_subshell = 1;
            } else if (c == '(' || c == ')') {
                result->has_subshell = 1;
            } else if (c == '>' || c == '<') {
                result->has_redirect = 1;
            }

            /* End current token if any */
            if (token_len > 0) {
                if (token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
            }

            /* Skip past the metacharacter(s) */
            p++;
            /* Skip second character of two-char operators */
            if ((c == '&' && *p == '&') || (c == '|' && *p == '|') ||
                (c == '>' && *p == '>') || (c == '<' && *p == '<')) {
                p++;
            }
            continue;
        }

        /* Regular character - accumulate into token */
        token_buf[token_len++] = c;
        p++;
    }

    /* Handle unbalanced quotes - mark as having chain to prevent matching */
    if (in_single_quote || in_double_quote) {
        result->has_chain = 1;
    }

    /* Add final token if any (or if we had empty quotes at the end) */
    if (token_len > 0 || had_quotes) {
        if (token_list_add(&tokens, token_buf, token_len) != 0) {
            free(token_buf);
            token_list_free(&tokens);
            return -1;
        }
    }

    free(token_buf);

    /* Transfer tokens to result */
    result->tokens = tokens.tokens;
    result->token_count = tokens.count;

    /* Don't free the token list itself - we've transferred ownership */
    return 0;
}

/* ============================================================================
 * Windows cmd.exe Parsing
 *
 * The full implementation is in shell_parser_cmd.c
 * ========================================================================== */

/* parse_cmd_shell() is implemented in shell_parser_cmd.c */

/* ============================================================================
 * PowerShell Parsing (Placeholder)
 *
 * NOTE: This is a conservative placeholder that falls back to POSIX-like
 * parsing with PowerShell-specific checks. Full PowerShell parsing would require:
 * - Different quoting rules (both ' and " but with different semantics)
 * - Script block syntax { }
 * - Pipeline chaining (;, |, &&, ||)
 * - Call operators (& and . at start of expression)
 * - Subexpression syntax $( )
 * - Here-strings (@' '@ and @" "@)
 *
 * For now, we err on the side of caution by flagging anything that looks
 * potentially dangerous.
 * ========================================================================== */

int parse_powershell(const char *command, ParsedShellCommand *result) {
    /* Fall back to POSIX-like parsing with PS-specific checks */
    if (parse_posix_shell(command, result) != 0) {
        return -1;
    }
    result->shell_type = SHELL_TYPE_POWERSHELL;

    if (!command) {
        return 0;
    }

    /* Add PowerShell-specific dangerous pattern check */
    if (powershell_command_is_dangerous(command)) {
        result->is_dangerous = 1;
    }

    /* Check for script blocks { } */
    if (strchr(command, '{') && strchr(command, '}')) {
        result->has_subshell = 1;
    }

    /* Check for call operators at start of command */
    /* Skip leading whitespace */
    const char *p = command;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '&' || *p == '.') {
        /* & or . at start indicates call operator */
        result->has_subshell = 1;
    }

    /* Check for subexpression syntax $( ) */
    if (strstr(command, "$(")) {
        result->has_subshell = 1;
    }

    return 0;
}

/* ============================================================================
 * Unified Parser Interface
 * ========================================================================== */

ParsedShellCommand *parse_shell_command(const char *command) {
    ShellType type = detect_shell_type();
    return parse_shell_command_for_type(command, type);
}

ParsedShellCommand *parse_shell_command_for_type(const char *command, ShellType type) {
    if (!command) {
        return NULL;
    }

    /* Reject overly long commands */
    if (strlen(command) > MAX_COMMAND_LENGTH) {
        return NULL;
    }

    ParsedShellCommand *result = calloc(1, sizeof(ParsedShellCommand));
    if (!result) {
        return NULL;
    }

    int parse_result;
    switch (type) {
        case SHELL_TYPE_CMD:
            parse_result = parse_cmd_shell(command, result);
            break;

        case SHELL_TYPE_POWERSHELL:
            parse_result = parse_powershell(command, result);
            break;

        case SHELL_TYPE_POSIX:
        case SHELL_TYPE_UNKNOWN:
        default:
            parse_result = parse_posix_shell(command, result);
            break;
    }

    if (parse_result != 0) {
        free(result);
        return NULL;
    }

    return result;
}

void free_parsed_shell_command(ParsedShellCommand *cmd) {
    if (!cmd) {
        return;
    }

    if (cmd->tokens) {
        for (int i = 0; i < cmd->token_count; i++) {
            free(cmd->tokens[i]);
        }
        free(cmd->tokens);
    }

    free(cmd);
}

/* ============================================================================
 * Allowlist Matching
 * ========================================================================== */

int shell_command_matches_prefix(const ParsedShellCommand *parsed,
                                 const char * const *prefix,
                                 int prefix_len) {
    if (!parsed || !prefix || prefix_len <= 0) {
        return 0;
    }

    /* Commands with chains/pipes/subshells/redirects never match */
    if (parsed->has_chain || parsed->has_pipe ||
        parsed->has_subshell || parsed->has_redirect) {
        return 0;
    }

    /* Dangerous commands never match */
    if (parsed->is_dangerous) {
        return 0;
    }

    /* Must have at least as many tokens as prefix */
    if (parsed->token_count < prefix_len) {
        return 0;
    }

    /* Check each prefix token matches */
    for (int i = 0; i < prefix_len; i++) {
        if (strcmp(parsed->tokens[i], prefix[i]) != 0) {
            return 0;
        }
    }

    return 1;
}

int commands_are_equivalent(const char *allowed_cmd,
                            const char *actual_cmd,
                            ShellType allowed_shell,
                            ShellType actual_shell) {
    if (!allowed_cmd || !actual_cmd) {
        return 0;
    }

    /* Exact match always works */
    if (strcmp(allowed_cmd, actual_cmd) == 0) {
        return 1;
    }

    /* Cross-platform equivalents */
    static const char *equivalents[][6] = {
        {"ls", "dir", "Get-ChildItem", "gci", "Get-Item", NULL},
        {"cat", "type", "Get-Content", "gc", NULL, NULL},
        {"pwd", "cd", "Get-Location", "gl", NULL, NULL},
        {"rm", "del", "erase", "Remove-Item", "ri", NULL},
        {"cp", "copy", "Copy-Item", "cpi", NULL, NULL},
        {"mv", "move", "ren", "Move-Item", "mi", NULL},
        {"echo", "Write-Output", "Write-Host", NULL, NULL, NULL},
        {"clear", "cls", "Clear-Host", NULL, NULL, NULL},
        {NULL, NULL, NULL, NULL, NULL, NULL}
    };

    /* Suppress unused parameter warnings */
    (void)allowed_shell;
    (void)actual_shell;

    for (int i = 0; equivalents[i][0]; i++) {
        int allowed_found = 0;
        int actual_found = 0;

        for (int j = 0; j < 6 && equivalents[i][j]; j++) {
            if (strcasecmp(allowed_cmd, equivalents[i][j]) == 0) {
                allowed_found = 1;
            }
            if (strcasecmp(actual_cmd, equivalents[i][j]) == 0) {
                actual_found = 1;
            }
        }

        if (allowed_found && actual_found) {
            return 1;
        }
    }

    return 0;
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

int shell_command_is_safe_for_matching(const ParsedShellCommand *parsed) {
    if (!parsed) {
        return 0;
    }

    /* Any of these flags prevents safe matching */
    if (parsed->has_chain || parsed->has_pipe ||
        parsed->has_subshell || parsed->has_redirect ||
        parsed->is_dangerous) {
        return 0;
    }

    return 1;
}

const char *shell_command_get_base(const ParsedShellCommand *parsed) {
    if (!parsed || parsed->token_count == 0) {
        return NULL;
    }
    return parsed->tokens[0];
}

ParsedShellCommand *copy_parsed_shell_command(const ParsedShellCommand *cmd) {
    if (!cmd) {
        return NULL;
    }

    ParsedShellCommand *copy = calloc(1, sizeof(ParsedShellCommand));
    if (!copy) {
        return NULL;
    }

    /* Copy scalar fields */
    copy->token_count = cmd->token_count;
    copy->has_chain = cmd->has_chain;
    copy->has_pipe = cmd->has_pipe;
    copy->has_subshell = cmd->has_subshell;
    copy->has_redirect = cmd->has_redirect;
    copy->is_dangerous = cmd->is_dangerous;
    copy->shell_type = cmd->shell_type;

    /* Copy tokens if any */
    if (cmd->token_count > 0) {
        copy->tokens = malloc((size_t)cmd->token_count * sizeof(char *));
        if (!copy->tokens) {
            free(copy);
            return NULL;
        }

        for (int i = 0; i < cmd->token_count; i++) {
            copy->tokens[i] = strdup(cmd->tokens[i]);
            if (!copy->tokens[i]) {
                /* Clean up on failure */
                for (int j = 0; j < i; j++) {
                    free(copy->tokens[j]);
                }
                free(copy->tokens);
                free(copy);
                return NULL;
            }
        }
    }

    return copy;
}
