/**
 * Windows cmd.exe Shell Command Parser Implementation
 *
 * Parses cmd.exe commands with proper handling of:
 * - Double quotes as the only string delimiters (single quotes are literal)
 * - Metacharacters: & | < > ^ %
 * - & as unconditional command separator (like ; in POSIX)
 * - ^ as escape character
 * - %VAR% as variable expansion
 *
 * The parser is intentionally conservative: commands containing any potentially
 * dangerous constructs are flagged and never auto-matched by allowlist entries.
 */

#include "shell_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal Constants
 * ========================================================================== */

/* Initial capacity for token array */
#define CMD_INITIAL_TOKEN_CAPACITY 16

/* Maximum command length we'll process */
#define CMD_MAX_COMMAND_LENGTH 65536

/* ============================================================================
 * Internal Helpers
 * ========================================================================== */

/**
 * Token accumulator for building parsed results.
 */
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

/**
 * Check if a character is a cmd.exe metacharacter.
 * cmd.exe metacharacters: & | < > ^ %
 */
static int is_cmd_metachar(char c) {
    return c == '&' || c == '|' || c == '<' || c == '>' || c == '^' || c == '%';
}

/* ============================================================================
 * cmd.exe Parser Implementation
 * ========================================================================== */

/**
 * Parse a Windows cmd.exe command.
 *
 * Parsing rules:
 * - Only double quotes are string delimiters (single quotes are literal)
 * - Detect metacharacters: & | < > ^ %
 * - & is unconditional separator (like ; in POSIX)
 * - ^ is escape character
 * - %VAR% is variable expansion (flagged as subshell for safety)
 */
int parse_cmd_shell(const char *command, ParsedShellCommand *result) {
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
    result->shell_type = SHELL_TYPE_CMD;

    /* Empty command is valid */
    if (!*command) {
        return 0;
    }

    CmdTokenList tokens;
    if (cmd_token_list_init(&tokens) != 0) {
        return -1;
    }

    /* Current token being built */
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

        /* Handle double quotes (the only string delimiter in cmd.exe) */
        if (c == '"') {
            in_double_quote = !in_double_quote;
            had_quotes = 1;
            p++;
            continue;
        }

        /* Inside double quotes, accumulate characters but still check for % */
        if (in_double_quote) {
            /* In cmd.exe, % variables expand even inside double quotes */
            if (c == '%') {
                result->has_subshell = 1;  /* Flag variable expansion */
            }
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        /* Outside quotes - check for whitespace */
        if (isspace((unsigned char)c)) {
            /* End current token if any (or if we had empty quotes) */
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

        /* Check for metacharacters */
        if (is_cmd_metachar(c)) {
            /* Detect specific patterns */
            if (c == '&') {
                /* & is unconditional separator, && is conditional AND */
                result->has_chain = 1;
            } else if (c == '|') {
                /* Check for || (conditional OR) */
                if (*(p + 1) == '|') {
                    result->has_chain = 1;
                } else {
                    result->has_pipe = 1;
                }
            } else if (c == '<' || c == '>') {
                result->has_redirect = 1;
            } else if (c == '^') {
                /*
                 * ^ is escape character in cmd.exe.
                 * It can escape any character including metacharacters.
                 * Mark as unsafe for matching.
                 */
                result->has_chain = 1;
            } else if (c == '%') {
                /*
                 * % indicates variable expansion in cmd.exe.
                 * %VAR%, %ERRORLEVEL%, %cd%, etc.
                 * Flag as subshell since it can execute arbitrary values.
                 */
                result->has_subshell = 1;
            }

            /* End current token if any */
            if (token_len > 0) {
                if (cmd_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    cmd_token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
            }

            /* Skip past the metacharacter(s) */
            p++;
            /* Skip second character of two-char operators */
            if ((c == '&' && *p == '&') || (c == '|' && *p == '|') ||
                (c == '>' && *p == '>')) {
                p++;
            }

            /* For ^ escape, skip the next character too */
            if (c == '^' && *p) {
                p++;
            }

            continue;
        }

        /* Regular character - accumulate into token */
        token_buf[token_len++] = c;
        p++;
    }

    /* Handle unbalanced quotes - mark as having chain to prevent matching */
    if (in_double_quote) {
        result->has_chain = 1;
    }

    /* Add final token if any (or if we had empty quotes at the end) */
    if (token_len > 0 || had_quotes) {
        if (cmd_token_list_add(&tokens, token_buf, token_len) != 0) {
            free(token_buf);
            cmd_token_list_free(&tokens);
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
