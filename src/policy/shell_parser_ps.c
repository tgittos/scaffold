#include "shell_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Initial capacity for token array */
#define PS_INITIAL_TOKEN_CAPACITY 16

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

static int is_ps_metachar(char c) {
    return c == ';' || c == '|' || c == '&' || c == '(' || c == ')' ||
           c == '{' || c == '}' || c == '$' || c == '`' || c == '>' || c == '<';
}

int parse_powershell(const char *command, ParsedShellCommand *result) {
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
                           powershell_command_is_dangerous(command);
    result->shell_type = SHELL_TYPE_POWERSHELL;

    if (!*command) {
        return 0;
    }

    PsTokenList tokens;
    if (ps_token_list_init(&tokens) != 0) {
        return -1;
    }

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

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            had_quotes = 1;
            at_expression_start = 0;
            p++;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            had_quotes = 1;
            at_expression_start = 0;
            p++;
            continue;
        }

        if (in_single_quote) {
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        if (in_double_quote) {
            if (c == '`' && *(p + 1)) {
                /* Backtick is escape character in PS double quotes */
                p++;
                token_buf[token_len++] = *p;
                p++;
                continue;
            }
            if (c == '$') {
                result->has_subshell = 1;
            }
            token_buf[token_len++] = c;
            p++;
            continue;
        }

        if (isspace((unsigned char)c)) {
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

        /* Backtick escapes make matching unsafe */
        if (c == '`') {
            result->has_chain = 1;  /* Escape sequences make matching unsafe */
            if (*(p + 1)) {
                p++;  /* Skip backtick */
                token_buf[token_len++] = *p;  /* Add escaped character */
                p++;
            } else {
                p++;  /* Line continuation */
            }
            at_expression_start = 0;
            continue;
        }

        /* Must check && before single & to avoid misidentifying as call operator */
        if (c == '&' && *(p + 1) == '&') {
            result->has_chain = 1;
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

        if (c == '&' && at_expression_start) {
            result->has_subshell = 1;
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

        /* Dot-source operator requires whitespace after the dot;
         * "./folder" is a path, not dot-source */
        if (c == '.' && at_expression_start) {
            char next = *(p + 1);
            if (next == ' ' || next == '\t') {
                result->has_subshell = 1;
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
        }

        if (is_ps_metachar(c)) {
            at_expression_start = 0;

            if (c == ';') {
                result->has_chain = 1;
            } else if (c == '|') {
                if (*(p + 1) == '|') {
                    result->has_chain = 1;
                } else {
                    result->has_pipe = 1;
                }
            } else if (c == '&') {
                /* Not at expression start and not &&; unusual, flag as unsafe */
                result->has_chain = 1;
            } else if (c == '$') {
                /* Both $() subexpressions and $var expansion make matching unsafe */
                result->has_subshell = 1;
            } else if (c == '{' || c == '}') {
                result->has_subshell = 1;
            } else if (c == '(' || c == ')') {
                result->has_subshell = 1;
            } else if (c == '>' || c == '<') {
                result->has_redirect = 1;
            }

            if (token_len > 0) {
                if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
                    free(token_buf);
                    ps_token_list_free(&tokens);
                    return -1;
                }
                token_len = 0;
            }

            p++;
            if ((c == '&' && *p == '&') || (c == '|' && *p == '|') ||
                (c == '>' && *p == '>')) {
                p++;
            }
            continue;
        }

        token_buf[token_len++] = c;
        at_expression_start = 0;
        p++;
    }

    /* Unbalanced quotes make matching unsafe */
    if (in_single_quote || in_double_quote) {
        result->has_chain = 1;
    }

    if (token_len > 0 || had_quotes) {
        if (ps_token_list_add(&tokens, token_buf, token_len) != 0) {
            free(token_buf);
            ps_token_list_free(&tokens);
            return -1;
        }
    }

    free(token_buf);

    result->tokens = tokens.tokens;
    result->token_count = tokens.count;
    return 0;
}
