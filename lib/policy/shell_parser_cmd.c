#include "shell_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Initial capacity for token array */
#define CMD_INITIAL_TOKEN_CAPACITY 16

/* Maximum command length we'll process */
#define CMD_MAX_COMMAND_LENGTH 65536

typedef struct {
    char **tokens;
    int count;
    int capacity;
} CmdTokenList;

static int cmd_token_list_init(CmdTokenList *list) {
    list->tokens = malloc(CMD_INITIAL_TOKEN_CAPACITY * sizeof(char *));
    if (!list->tokens) {
        return -1;
    }
    list->count = 0;
    list->capacity = CMD_INITIAL_TOKEN_CAPACITY;
    return 0;
}

static int cmd_token_list_add(CmdTokenList *list, const char *start, size_t len) {
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

static void cmd_token_list_free(CmdTokenList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->tokens[i]);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int is_cmd_metachar(char c) {
    return c == '&' || c == '|' || c == '<' || c == '>' || c == '^' || c == '%';
}

static const char *CMD_DANGEROUS_PATTERNS[] = {
    "format ",      /* Format disk */
    "del /s",       /* Recursive delete */
    "del /q",       /* Quiet delete (no confirmation) */
    "rd /s",        /* Recursive directory removal */
    "rmdir /s",     /* Recursive directory removal */
    "diskpart",     /* Disk partitioning */
    "bcdedit",      /* Boot configuration */
    "reg delete",   /* Registry deletion */
    "powershell",   /* PowerShell invocation from cmd */
    "pwsh",         /* PowerShell Core invocation */
    NULL
};

static int cmd_command_is_dangerous(const char *command) {
    if (!command || !*command) {
        return 0;
    }

    size_t len = strlen(command);
    char *lower = malloc(len + 1);
    if (!lower) {
        return 0;
    }

    for (size_t i = 0; i <= len; i++) {
        lower[i] = (char)tolower((unsigned char)command[i]);
    }

    int is_dangerous = 0;
    for (int i = 0; CMD_DANGEROUS_PATTERNS[i]; i++) {
        if (strstr(lower, CMD_DANGEROUS_PATTERNS[i])) {
            is_dangerous = 1;
            break;
        }
    }

    free(lower);
    return is_dangerous;
}

int parse_cmd_shell(const char *command, ParsedShellCommand *result) {
    if (!command || !result) {
        return -1;
    }

    result->tokens = NULL;
    result->token_count = 0;
    result->has_chain = 0;
    result->has_pipe = 0;
    result->has_subshell = 0;
    result->has_redirect = 0;
    result->is_dangerous = shell_command_is_dangerous(command) ||
                           cmd_command_is_dangerous(command);
    result->shell_type = SHELL_TYPE_CMD;

    if (!*command) {
        return 0;
    }

    CmdTokenList tokens;
    if (cmd_token_list_init(&tokens) != 0) {
        return -1;
    }

    char *token_buf = malloc(strlen(command) + 1);
    if (!token_buf) {
        cmd_token_list_free(&tokens);
        return -1;
    }
    size_t token_len = 0;

    const char *p = command;
    int in_double_quote = 0;
    int had_quotes = 0;  /* Track if we've seen quotes for current token */

    while (*p) {
        char c = *p;

        /*
         * Security: Check for non-ASCII characters.
         * Unicode lookalikes could bypass detection.
         */
        if ((unsigned char)c > 127) {
            result->has_chain = 1;
        }

        if (c == '"') {
            in_double_quote = !in_double_quote;
            had_quotes = 1;
            p++;
            continue;
        }

        if (in_double_quote) {
            /* cmd.exe expands %VAR% even inside double quotes */
            if (c == '%') {
                result->has_subshell = 1;
            }
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        if (isspace((unsigned char)c)) {
            if (token_len > 0 || had_quotes) {
                if (cmd_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    cmd_token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
                had_quotes = 0;
            }
            p++;
            continue;
        }

        if (is_cmd_metachar(c)) {
            if (c == '&') {
                result->has_chain = 1;
            } else if (c == '|') {
                if (*(p + 1) == '|') {
                    result->has_chain = 1;
                } else {
                    result->has_pipe = 1;
                }
            } else if (c == '<' || c == '>') {
                result->has_redirect = 1;
            } else if (c == '^') {
                /* ^ can escape metacharacters; makes matching unsafe */
                result->has_chain = 1;
            } else if (c == '%') {
                /* %VAR% expansion can inject arbitrary values */
                result->has_subshell = 1;
            }

            if (token_len > 0) {
                if (cmd_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    cmd_token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
            }

            p++;
            if ((c == '&' && *p == '&') || (c == '|' && *p == '|') ||
                (c == '>' && *p == '>')) {
                p++;
            }

            if (c == '^' && *p) {
                p++;
            }

            continue;
        }

        token_buf[token_len++] = c;
        p++;
    }

    /* Unbalanced quotes make matching unsafe */
    if (in_double_quote) {
        result->has_chain = 1;
    }

    if (token_len > 0 || had_quotes) {
        if (cmd_token_list_add(&tokens, token_buf, token_len) != 0) {
            free(token_buf);
            cmd_token_list_free(&tokens);
            return -1;
        }
    }

    free(token_buf);

    result->tokens = tokens.tokens;
    result->token_count = tokens.count;
    return 0;
}
