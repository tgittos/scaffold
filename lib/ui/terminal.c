#include "terminal.h"
#include "../../src/utils/output_formatter.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

bool terminal_colors_enabled(void) {
    if (get_json_output_mode()) {
        return false;
    }
    return isatty(STDOUT_FILENO) != 0;
}

void terminal_separator(FILE *out, TerminalSeparatorStyle style, int width) {
    if (out == NULL || width <= 0) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    const char *ch = (style == TERM_SEP_HEAVY) ? TERM_BOX_HEAVY_H : TERM_BOX_LIGHT_H;

    for (int i = 0; i < width; i++) {
        fputs(ch, out);
    }
    fputc('\n', out);
    fflush(out);
}

void terminal_header(FILE *out, const char *title, int width) {
    if (out == NULL || width <= 0) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    fputc('\n', out);
    terminal_separator(out, TERM_SEP_HEAVY, width);

    if (title != NULL && *title != '\0') {
        if (terminal_colors_enabled()) {
            fprintf(out, TERM_BOLD "%s" TERM_RESET "\n", title);
        } else {
            fprintf(out, "%s\n", title);
        }
    }

    terminal_separator(out, TERM_SEP_HEAVY, width);
    fflush(out);
}

void terminal_tree_branch(FILE *out, bool is_last, int indent) {
    if (out == NULL) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    for (int i = 0; i < indent; i++) {
        fputc(' ', out);
    }

    if (is_last) {
        fputs(TERM_TREE_LAST, out);
    } else {
        fputs(TERM_TREE_BRANCH, out);
    }
    fputc(' ', out);
}

void terminal_tree_item(FILE *out, const char *text, bool is_last, int indent) {
    if (out == NULL) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    terminal_tree_branch(out, is_last, indent);

    if (text != NULL) {
        fputs(text, out);
    }
    fputc('\n', out);
    fflush(out);
}

static void get_status_symbol_and_color(TerminalStatusType type,
                                         const char **symbol,
                                         const char **color) {
    switch (type) {
        case TERM_STATUS_SUCCESS:
            *symbol = TERM_SYM_SUCCESS;
            *color = TERM_GREEN;
            break;
        case TERM_STATUS_ERROR:
            *symbol = TERM_SYM_ERROR;
            *color = TERM_RED;
            break;
        case TERM_STATUS_INFO:
            *symbol = TERM_SYM_INFO;
            *color = TERM_YELLOW;
            break;
        case TERM_STATUS_ACTIVE:
            *symbol = TERM_SYM_ACTIVE;
            *color = TERM_CYAN;
            break;
    }
}

void terminal_status(FILE *out, TerminalStatusType type, const char *message) {
    if (out == NULL) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    const char *symbol = NULL;
    const char *color = NULL;
    get_status_symbol_and_color(type, &symbol, &color);

    if (terminal_colors_enabled()) {
        fprintf(out, "%s%s" TERM_RESET " %s\n", color, symbol, message ? message : "");
    } else {
        fprintf(out, "%s %s\n", symbol, message ? message : "");
    }
    fflush(out);
}

void terminal_status_with_detail(FILE *out, TerminalStatusType type,
                                  const char *message, const char *detail) {
    if (out == NULL) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    const char *symbol = NULL;
    const char *color = NULL;
    get_status_symbol_and_color(type, &symbol, &color);

    if (terminal_colors_enabled()) {
        if (detail != NULL && *detail != '\0') {
            fprintf(out, "%s%s" TERM_RESET " %s" TERM_DIM " (%s)" TERM_RESET "\n",
                    color, symbol, message ? message : "", detail);
        } else {
            fprintf(out, "%s%s" TERM_RESET " %s\n", color, symbol, message ? message : "");
        }
    } else {
        if (detail != NULL && *detail != '\0') {
            fprintf(out, "%s %s (%s)\n", symbol, message ? message : "", detail);
        } else {
            fprintf(out, "%s %s\n", symbol, message ? message : "");
        }
    }
    fflush(out);
}

void terminal_labeled(FILE *out, const char *label, const char *value) {
    if (out == NULL) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    if (terminal_colors_enabled()) {
        fprintf(out, TERM_BOLD "%s:" TERM_RESET " %s\n",
                label ? label : "", value ? value : "");
    } else {
        fprintf(out, "%s: %s\n", label ? label : "", value ? value : "");
    }
    fflush(out);
}

void terminal_clear_line(FILE *out) {
    if (out == NULL) {
        return;
    }
    if (get_json_output_mode()) {
        return;
    }

    fprintf(out, "\r\033[K");
    fflush(out);
}

char *terminal_strip_ansi(const char *str) {
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\033' || str[i] == '\x1b') {
            /* Skip ESC [ <params> <intermediate> <final> sequences (CSI)
             * Parameters: 0x30-0x3F (0-9:;<=>?)
             * Intermediate: 0x20-0x2F (space !"#$%&'()*+,-./)
             * Final: 0x40-0x7E (@A-Z[\]^_`a-z{|}~)
             */
            if (i + 1 < len && str[i + 1] == '[') {
                i += 2;
                /* Skip parameter bytes: 0-9 : ; < = > ? */
                while (i < len && ((unsigned char)str[i] >= 0x30 &&
                                   (unsigned char)str[i] <= 0x3F)) {
                    i++;
                }
                /* Skip intermediate bytes: space through / */
                while (i < len && ((unsigned char)str[i] >= 0x20 &&
                                   (unsigned char)str[i] <= 0x2F)) {
                    i++;
                }
                /* Skip final byte: @ through ~ */
                if (i < len && ((unsigned char)str[i] >= 0x40 &&
                                (unsigned char)str[i] <= 0x7E)) {
                    continue;
                }
                /* Malformed sequence - back up one so outer loop handles it */
                if (i > 0) i--;
                continue;
            }
        } else if (str[i] == '\r') {
            /* Skip carriage returns (often used with line clearing) */
            continue;
        }
        result[j++] = str[i];
    }
    result[j] = '\0';
    return result;
}
