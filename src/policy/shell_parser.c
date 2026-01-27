#include "shell_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Initial capacity for token array */
#define INITIAL_TOKEN_CAPACITY 16

/* Maximum command length we'll process */
#define MAX_COMMAND_LENGTH 65536

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

static const char *DISK_WRITE_PATTERNS[] = {
    "of=/dev/sd",
    "of=/dev/hd",
    "of=/dev/nvme",
    "> /dev/sd",
    "> /dev/hd",
    "> /dev/nvme",
    NULL
};

ShellType detect_shell_type(void) {
#ifdef _WIN32
    const char *psmodule = getenv("PSModulePath");
    const char *comspec = getenv("COMSPEC");

    /* PSModulePath is only set inside a PowerShell session */
    if (psmodule && strlen(psmodule) > 0) {
        return SHELL_TYPE_POWERSHELL;
    }

    if (comspec) {
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

    return SHELL_TYPE_CMD;
#else
    const char *shell = getenv("SHELL");
    if (shell) {
        if (strstr(shell, "pwsh") || strstr(shell, "powershell")) {
            return SHELL_TYPE_POWERSHELL;
        }
    }

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

int shell_command_is_dangerous(const char *command) {
    if (!command || !*command) {
        return 0;
    }

    for (int i = 0; DANGEROUS_PATTERNS[i]; i++) {
        if (strstr(command, DANGEROUS_PATTERNS[i])) {
            return 1;
        }
    }

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

    for (int i = 0; DISK_WRITE_PATTERNS[i]; i++) {
        if (strstr(command, DISK_WRITE_PATTERNS[i])) {
            return 1;
        }
    }

    if (strstr(command, "dd ") && strstr(command, "of=/dev/")) {
        return 1;
    }

    return 0;
}

int powershell_command_is_dangerous(const char *command) {
    if (!command || !*command) {
        return 0;
    }

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

static int is_posix_metachar(char c) {
    return c == ';' || c == '|' || c == '&' || c == '(' || c == ')' ||
           c == '$' || c == '`' || c == '>' || c == '<';
}

int parse_posix_shell(const char *command, ParsedShellCommand *result) {
    if (!command || !result) {
        return -1;
    }

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

        if (in_single_quote || in_double_quote) {
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        if (isspace((unsigned char)c)) {
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

        if (is_posix_metachar(c)) {
            if (c == ';') {
                result->has_chain = 1;
            } else if (c == '|') {
                if (*(p + 1) == '|') {
                    result->has_chain = 1;
                } else {
                    result->has_pipe = 1;
                }
            } else if (c == '&') {
                if (*(p + 1) == '&') {
                    result->has_chain = 1;
                }
                /* Single & is background, which we'll treat as chain for safety */
                else {
                    result->has_chain = 1;
                }
            } else if (c == '$') {
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

            if (token_len > 0) {
                if (token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
            }

            p++;
            if ((c == '&' && *p == '&') || (c == '|' && *p == '|') ||
                (c == '>' && *p == '>') || (c == '<' && *p == '<')) {
                p++;
            }
            continue;
        }

        token_buf[token_len++] = c;
        p++;
    }

    /* Unbalanced quotes make matching unsafe */
    if (in_single_quote || in_double_quote) {
        result->has_chain = 1;
    }

    if (token_len > 0 || had_quotes) {
        if (token_list_add(&tokens, token_buf, token_len) != 0) {
            free(token_buf);
            token_list_free(&tokens);
            return -1;
        }
    }

    free(token_buf);

    result->tokens = tokens.tokens;
    result->token_count = tokens.count;
    return 0;
}

ParsedShellCommand *parse_shell_command(const char *command) {
    ShellType type = detect_shell_type();
    return parse_shell_command_for_type(command, type);
}

ParsedShellCommand *parse_shell_command_for_type(const char *command, ShellType type) {
    if (!command) {
        return NULL;
    }

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

int shell_command_matches_prefix(const ParsedShellCommand *parsed,
                                 const char * const *prefix,
                                 int prefix_len) {
    if (!parsed || !prefix || prefix_len <= 0) {
        return 0;
    }

    if (parsed->has_chain || parsed->has_pipe ||
        parsed->has_subshell || parsed->has_redirect) {
        return 0;
    }

    if (parsed->is_dangerous) {
        return 0;
    }

    if (parsed->token_count < prefix_len) {
        return 0;
    }

    for (int i = 0; i < prefix_len; i++) {
        if (strcmp(parsed->tokens[i], prefix[i]) != 0) {
            return 0;
        }
    }

    return 1;
}

int commands_are_equivalent(const char *allowed_cmd,
                            const char *actual_cmd) {
    if (!allowed_cmd || !actual_cmd) {
        return 0;
    }

    if (strcmp(allowed_cmd, actual_cmd) == 0) {
        return 1;
    }

    /*
     * Cross-platform equivalents.
     *
     * Only commands with truly equivalent behavior are included:
     * - Get-Item is NOT equivalent to ls/dir (it gets a single item, not a listing)
     * - cd is NOT equivalent to pwd (cd with arguments changes directory)
     */
    static const char *equivalents[][6] = {
        {"ls", "dir", "Get-ChildItem", "gci", NULL, NULL},
        {"cat", "type", "Get-Content", "gc", NULL, NULL},
        {"pwd", "Get-Location", "gl", NULL, NULL, NULL},
        {"rm", "del", "erase", "Remove-Item", "ri", NULL},
        {"cp", "copy", "Copy-Item", "cpi", NULL, NULL},
        {"mv", "move", "ren", "Move-Item", "mi", NULL},
        {"echo", "Write-Output", "Write-Host", NULL, NULL, NULL},
        {"clear", "cls", "Clear-Host", NULL, NULL, NULL},
        {NULL, NULL, NULL, NULL, NULL, NULL}
    };

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

int shell_command_is_safe_for_matching(const ParsedShellCommand *parsed) {
    if (!parsed) {
        return 0;
    }

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

    copy->token_count = cmd->token_count;
    copy->has_chain = cmd->has_chain;
    copy->has_pipe = cmd->has_pipe;
    copy->has_subshell = cmd->has_subshell;
    copy->has_redirect = cmd->has_redirect;
    copy->is_dangerous = cmd->is_dangerous;
    copy->shell_type = cmd->shell_type;

    if (cmd->token_count > 0) {
        copy->tokens = malloc((size_t)cmd->token_count * sizeof(char *));
        if (!copy->tokens) {
            free(copy);
            return NULL;
        }

        for (int i = 0; i < cmd->token_count; i++) {
            copy->tokens[i] = strdup(cmd->tokens[i]);
            if (!copy->tokens[i]) {
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
