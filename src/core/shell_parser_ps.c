/**
 * PowerShell Command Parser Implementation
 *
 * Parses PowerShell commands with proper handling of:
 * - Single and double quotes (both are string delimiters)
 * - Metacharacters: ; && || | $() {} > >> <
 * - & and . as call operators at expression start
 * - $variable expansion
 * - Script blocks {}
 * - Subexpressions $()
 *
 * The parser is intentionally conservative: commands containing any potentially
 * dangerous constructs are flagged and never auto-matched by allowlist entries.
 *
 * PowerShell-specific dangerous patterns include cmdlets like:
 * - Invoke-Expression (iex)
 * - Invoke-Command (icm)
 * - Start-Process
 * - -EncodedCommand (-enc)
 * - DownloadString/DownloadFile
 */

#include "shell_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal Constants
 * ========================================================================== */

/* Initial capacity for token array */
#define PS_INITIAL_TOKEN_CAPACITY 16

/* Maximum command length we'll process */
#define PS_MAX_COMMAND_LENGTH 65536

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
} PsTokenList;

static int ps_token_list_init(PsTokenList *list) {
    list->tokens = malloc(PS_INITIAL_TOKEN_CAPACITY * sizeof(char *));
    if (!list->tokens) {
        return -1;
    }
    list->count = 0;
    list->capacity = PS_INITIAL_TOKEN_CAPACITY;
    return 0;
}

static int ps_token_list_add(PsTokenList *list, const char *start, size_t len) {
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

static void ps_token_list_free(PsTokenList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->tokens[i]);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

/**
 * Check if a character is a PowerShell metacharacter (outside quotes).
 * PowerShell metacharacters: ; | & ( ) { } $ ` > <
 *
 * Note: && and || are pipeline chain operators in PowerShell 7+.
 */
static int is_ps_metachar(char c) {
    return c == ';' || c == '|' || c == '&' || c == '(' || c == ')' ||
           c == '{' || c == '}' || c == '$' || c == '`' || c == '>' || c == '<';
}

/* ============================================================================
 * PowerShell Parser Implementation
 * ========================================================================== */

/**
 * Parse a PowerShell command.
 *
 * Parsing rules:
 * - Both single and double quotes are string delimiters
 * - Single quotes: literal content, no escape sequences
 * - Double quotes: allow variable expansion and escape with backtick
 * - Detect metacharacters: ; | & ( ) { } $ ` > <
 * - ; is command separator
 * - && and || are pipeline chain operators (PS 7+)
 * - | is pipe
 * - $() is subexpression
 * - {} is script block (treated as subshell for safety)
 * - & at start of expression is call operator
 * - . at start of expression is dot-source operator
 * - $var is variable expansion (treated as subshell for safety)
 * - ` is escape character (like \ in POSIX)
 */
int parse_powershell(const char *command, ParsedShellCommand *result) {
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
    result->is_dangerous = shell_command_is_dangerous(command) ||
                           powershell_command_is_dangerous(command);
    result->shell_type = SHELL_TYPE_POWERSHELL;

    /* Empty command is valid */
    if (!*command) {
        return 0;
    }

    PsTokenList tokens;
    if (ps_token_list_init(&tokens) != 0) {
        return -1;
    }

    /* Current token being built */
    char *token_buf = malloc(strlen(command) + 1);
    if (!token_buf) {
        ps_token_list_free(&tokens);
        return -1;
    }
    size_t token_len = 0;

    const char *p = command;
    int in_single_quote = 0;
    int in_double_quote = 0;
    int had_quotes = 0;  /* Track if we've seen quotes for current token */
    int at_expression_start = 1;  /* Track if we're at start of expression */

    while (*p) {
        char c = *p;

        /*
         * Security: Check for non-ASCII characters.
         * Unicode lookalikes could bypass detection.
         */
        if ((unsigned char)c > 127) {
            result->has_chain = 1;
        }

        /* Handle single quotes */
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            had_quotes = 1;
            at_expression_start = 0;
            p++;
            continue;
        }

        /* Handle double quotes */
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            had_quotes = 1;
            at_expression_start = 0;
            p++;
            continue;
        }

        /* Inside single quotes - literal content, no interpretation */
        if (in_single_quote) {
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        /* Inside double quotes - check for backtick escape and variables */
        if (in_double_quote) {
            if (c == '`' && *(p + 1)) {
                /* Backtick is escape character in double quotes */
                /* Skip the backtick and include the next character */
                p++;
                token_buf[token_len++] = *p;
                p++;
                continue;
            }
            if (c == '$') {
                /* Variable expansion in double quotes */
                result->has_subshell = 1;
            }
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        /* Outside quotes - check for whitespace */
        if (isspace((unsigned char)c)) {
            /* End current token if any (or if we had empty quotes) */
            if (token_len > 0 || had_quotes) {
                if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    ps_token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
                had_quotes = 0;
            }
            at_expression_start = 1;  /* Whitespace resets expression start */
            p++;
            continue;
        }

        /*
         * Handle backtick escape outside quotes.
         * In PowerShell, backtick is the escape character.
         */
        if (c == '`') {
            result->has_chain = 1;  /* Escape sequences make matching unsafe */
            if (*(p + 1)) {
                p += 2;  /* Skip backtick and escaped character */
            } else {
                p++;  /* Line continuation */
            }
            continue;
        }

        /*
         * Check for && chain operator BEFORE checking for call operator.
         * && is pipeline chain operator in PS 7+, should always be flagged.
         */
        if (c == '&' && *(p + 1) == '&') {
            result->has_chain = 1;
            /* End current token if any */
            if (token_len > 0) {
                if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    ps_token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
            }
            p += 2;  /* Skip both & characters */
            at_expression_start = 1;  /* Next thing is start of new expression */
            continue;
        }

        /* Check for call operator & at start of expression (single &) */
        if (c == '&' && at_expression_start) {
            result->has_subshell = 1;  /* Call operator */
            if (token_len > 0) {
                if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    ps_token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
            }
            p++;
            continue;
        }

        /*
         * Check for dot-source operator . at start of expression.
         * Dot-source requires a space/tab after the dot: ". script.ps1"
         * A path like "./folder" is NOT dot-source (no space after dot).
         */
        if (c == '.' && at_expression_start) {
            char next = *(p + 1);
            if (next == ' ' || next == '\t') {
                result->has_subshell = 1;  /* Dot-source operator */
                if (token_len > 0) {
                    if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
                        free(token_buf);
                        ps_token_list_free(&tokens);
                        return -1;
                    }
                    token_len = 0;
                }
                p++;
                continue;
            }
            /* Otherwise, . is part of a path like ./folder, not dot-source */
        }

        /* Check for metacharacters */
        if (is_ps_metachar(c)) {
            at_expression_start = 0;

            /* Detect specific patterns */
            if (c == ';') {
                result->has_chain = 1;
            } else if (c == '|') {
                /* Check for || (conditional OR, PS 7+) */
                if (*(p + 1) == '|') {
                    result->has_chain = 1;
                } else {
                    result->has_pipe = 1;
                }
            } else if (c == '&') {
                /* Check for && (conditional AND, PS 7+) */
                if (*(p + 1) == '&') {
                    result->has_chain = 1;
                }
                /* Single & not at expression start is handled above */
            } else if (c == '$') {
                /* Check for $( subexpression */
                if (*(p + 1) == '(') {
                    result->has_subshell = 1;
                } else {
                    /* Variable expansion - $var */
                    result->has_subshell = 1;  /* Variables make matching unsafe */
                }
            } else if (c == '{' || c == '}') {
                /* Script blocks */
                result->has_subshell = 1;
            } else if (c == '(' || c == ')') {
                /* Subexpressions and grouping */
                result->has_subshell = 1;
            } else if (c == '>' || c == '<') {
                result->has_redirect = 1;
            }

            /* End current token if any */
            if (token_len > 0) {
                if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    ps_token_list_free(&tokens);
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
            continue;
        }

        /* Regular character - accumulate into token */
        token_buf[token_len++] = c;
        at_expression_start = 0;
        p++;
    }

    /* Handle unbalanced quotes - mark as having chain to prevent matching */
    if (in_single_quote || in_double_quote) {
        result->has_chain = 1;
    }

    /* Add final token if any (or if we had empty quotes at the end) */
    if (token_len > 0 || had_quotes) {
        if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
            free(token_buf);
            ps_token_list_free(&tokens);
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
