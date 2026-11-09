#include "status_line.h"
#include "output_formatter.h"
#include "../agent/prompt_mode.h"
#include "../util/ansi_codes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define STATUS_MAX_AGENTS 8
#define STATUS_AGENT_TASK_LEN 64
#define STATUS_MODEL_NAME_LEN 96

typedef struct {
    char id_short[5];
    char task[STATUS_AGENT_TASK_LEN];
    time_t start_time;
} AgentSummary;

typedef struct {
    pthread_mutex_t mutex;
    bool initialized;

    int active_agent_count;
    AgentSummary agents[STATUS_MAX_AGENTS];

    bool system_busy;
    char activity_label[64];
    time_t activity_start_time;

    int session_prompt_tokens;
    int session_completion_tokens;
    int last_response_tokens;
    bool busy_rendered;

    int current_mode;

    char model_name[STATUS_MODEL_NAME_LEN];
    int context_window;
} StatusLineState;

static StatusLineState g_status = {0};

/* Main-thread-only flag — render_info() and clear_rendered() are
 * only called from the REPL main thread. */
static bool g_info_rendered = false;

void status_line_init(void) {
    memset(&g_status, 0, sizeof(g_status));
    pthread_mutex_init(&g_status.mutex, NULL);
    g_status.initialized = true;
    g_info_rendered = false;
}

void status_line_cleanup(void) {
    if (!g_status.initialized) return;
    pthread_mutex_destroy(&g_status.mutex);
    g_status.initialized = false;
    g_info_rendered = false;
}

void status_line_update_agents(int count, const StatusAgentInfo *agents) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);

    int n = 0;
    for (int i = 0; i < count && n < STATUS_MAX_AGENTS; i++) {
        AgentSummary *a = &g_status.agents[n];
        if (agents[i].id) {
            memcpy(a->id_short, agents[i].id, 4);
            a->id_short[4] = '\0';
        } else {
            a->id_short[0] = '\0';
        }
        if (agents[i].task) {
            snprintf(a->task, STATUS_AGENT_TASK_LEN, "%s", agents[i].task);
        } else {
            a->task[0] = '\0';
        }
        a->start_time = agents[i].start_time;
        n++;
    }
    g_status.active_agent_count = n;

    pthread_mutex_unlock(&g_status.mutex);
}

void status_line_set_busy(const char *label) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);
    g_status.system_busy = true;
    if (label) {
        snprintf(g_status.activity_label, sizeof(g_status.activity_label), "%s", label);
    } else {
        g_status.activity_label[0] = '\0';
    }
    g_status.activity_start_time = time(NULL);
    bool should_render = !get_json_output_mode();
    if (should_render) {
        g_status.busy_rendered = true;
    }
    pthread_mutex_unlock(&g_status.mutex);

    if (should_render) {
        fprintf(stdout, TERM_CYAN TERM_SYM_ACTIVE TERM_RESET " ");
        fflush(stdout);
    }
}

void status_line_set_idle(void) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);
    g_status.system_busy = false;
    g_status.activity_label[0] = '\0';
    bool was_rendered = g_status.busy_rendered;
    g_status.busy_rendered = false;
    pthread_mutex_unlock(&g_status.mutex);

    if (was_rendered) {
        fprintf(stdout, TERM_CLEAR_LINE);
        fflush(stdout);
    }
}

void status_line_update_tokens(int prompt_tokens, int completion_tokens) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);
    if (prompt_tokens > 0) g_status.session_prompt_tokens = prompt_tokens;
    if (completion_tokens > 0) g_status.session_completion_tokens += completion_tokens;
    pthread_mutex_unlock(&g_status.mutex);
}

void status_line_set_last_response_tokens(int tokens) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);
    g_status.last_response_tokens = tokens;
    pthread_mutex_unlock(&g_status.mutex);
}

void status_line_set_model(const char *model, int context_window) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);
    snprintf(g_status.model_name, sizeof(g_status.model_name), "%s", model ? model : "");
    g_status.context_window = context_window;
    pthread_mutex_unlock(&g_status.mutex);
}

void status_line_invalidate_context(void) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);
    g_status.session_prompt_tokens = 0;
    pthread_mutex_unlock(&g_status.mutex);
}

static void format_token_count(int tokens, char *buf, size_t buf_size) {
    if (tokens >= 10000000) {
        snprintf(buf, buf_size, "%dM", (tokens + 500000) / 1000000);
    } else if (tokens >= 1000000) {
        snprintf(buf, buf_size, "%.1fM", tokens / 1000000.0);
    } else if (tokens >= 10000) {
        snprintf(buf, buf_size, "%dk", (tokens + 500) / 1000);
    } else if (tokens >= 1000) {
        snprintf(buf, buf_size, "%.1fk", tokens / 1000.0);
    } else {
        snprintf(buf, buf_size, "%d", tokens);
    }
}

static void format_context(char *buf, size_t buf_size, bool with_model) {
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';
    if (!g_status.initialized) return;

    pthread_mutex_lock(&g_status.mutex);
    char model[STATUS_MODEL_NAME_LEN];
    memcpy(model, g_status.model_name, sizeof(model));
    int window = g_status.context_window;
    int tokens = g_status.session_prompt_tokens;
    pthread_mutex_unlock(&g_status.mutex);

    if (window <= 0 && !model[0]) return;

    char ctx[48] = {0};
    const char *color = "";
    if (window > 0) {
        char window_str[16] = {0};
        format_token_count(window, window_str, sizeof(window_str));
        if (tokens > 0) {
            double pct = tokens * 100.0 / window;
            if (pct > 90.0) color = TERM_BRIGHT_RED;
            else if (pct > 70.0) color = TERM_BRIGHT_YELLOW;
            snprintf(ctx, sizeof(ctx), "%.1f%%/%s", pct, window_str);
        } else {
            snprintf(ctx, sizeof(ctx), "?/%s", window_str);
        }
    } else {
        snprintf(ctx, sizeof(ctx), "?");
    }

    const char *reset = color[0] ? TERM_RESET : "";
    if (with_model && model[0]) {
        snprintf(buf, buf_size, "%s%s%s  " TERM_DIM "%s" TERM_RESET, color, ctx, reset, model);
    } else {
        snprintf(buf, buf_size, "%s%s%s", color, ctx, reset);
    }
}

void status_line_format_context(char *buf, size_t buf_size) {
    format_context(buf, buf_size, true);
}

/* 0 when unknown (no TTY and no $COLUMNS): rendering is then unclamped. */
static int terminal_columns(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    const char *env = getenv("COLUMNS");
    int cols = env ? atoi(env) : 0;
    return cols > 0 ? cols : 0;
}

/* Terminal columns used by s: skips CSI escapes, counts UTF-8 code points. */
static int visible_width(const char *s) {
    int width = 0;
    while (*s) {
        if (s[0] == '\033' && s[1] == '[') {
            s += 2;
            while (*s && !(*s >= '@' && *s <= '~')) s++;
            if (*s) s++;
            continue;
        }
        if (((unsigned char)*s & 0xC0) != 0x80) width++;
        s++;
    }
    return width;
}

void status_line_set_mode(int mode) {
    if (!g_status.initialized) return;
    pthread_mutex_lock(&g_status.mutex);
    g_status.current_mode = mode;
    pthread_mutex_unlock(&g_status.mutex);
}

#define RL_START "\001"
#define RL_END   "\002"

void status_line_render_info(void) {
    if (!g_status.initialized) return;
    if (get_json_output_mode()) return;

    pthread_mutex_lock(&g_status.mutex);

    int agent_count = g_status.active_agent_count;
    AgentSummary agents[STATUS_MAX_AGENTS];
    memcpy(agents, g_status.agents, sizeof(agents));
    /* Context size (latest prompt) + total generated output */
    int session_total = g_status.session_prompt_tokens + g_status.session_completion_tokens;
    int last_response = g_status.last_response_tokens;
    bool busy = g_status.system_busy;
    char busy_label[64];
    time_t busy_start = g_status.activity_start_time;
    if (busy) {
        memcpy(busy_label, g_status.activity_label, sizeof(busy_label));
    }

    pthread_mutex_unlock(&g_status.mutex);

    char left[512] = "";
    time_t now = time(NULL);

    if (busy) {
        int elapsed = (int)(now - busy_start);
        snprintf(left, sizeof(left), TERM_DIM "  " TERM_SYM_ACTIVE " %s (%ds)" TERM_RESET,
                 busy_label[0] ? busy_label : "Working...", elapsed);
    } else if (agent_count > 0) {
        char line[384];
        int pos = 0;
        pos += snprintf(line + pos, sizeof(line) - pos,
                        "  %d agent%s: ", agent_count, agent_count > 1 ? "s" : "");
        for (int i = 0; i < agent_count && i < 3; i++) {
            int elapsed = (int)(now - agents[i].start_time);
            if (i > 0) {
                pos += snprintf(line + pos, sizeof(line) - pos, ", ");
            }
            char short_task[21];
            if (strlen(agents[i].task) > 20) {
                snprintf(short_task, sizeof(short_task), "%.17s...", agents[i].task);
            } else {
                snprintf(short_task, sizeof(short_task), "%s", agents[i].task);
            }
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "%s (%ds)", short_task, elapsed);
        }
        if (agent_count > 3) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            ", +%d more", agent_count - 3);
        }

        if (session_total > 0) {
            char session_str[16];
            format_token_count(session_total, session_str, sizeof(session_str));
            snprintf(left, sizeof(left), TERM_DIM "%s       %s tokens" TERM_RESET,
                     line, session_str);
        } else {
            snprintf(left, sizeof(left), TERM_DIM "%s" TERM_RESET, line);
        }
    } else if (last_response > 0) {
        char resp_str[16];
        format_token_count(last_response, resp_str, sizeof(resp_str));
        if (session_total > 0) {
            char session_str[16];
            format_token_count(session_total, session_str, sizeof(session_str));
            snprintf(left, sizeof(left), TERM_DIM "  \xe2\x94\x94 %s tokens"
                     "       %s session" TERM_RESET, resp_str, session_str);
        } else {
            snprintf(left, sizeof(left), TERM_DIM "  \xe2\x94\x94 %s tokens" TERM_RESET,
                     resp_str);
        }
    }

    /* status_line_clear_rendered() erases one row, so the line must not wrap:
     * drop the model name, then the whole segment, to fit. */
    char segment[256] = {0};
    format_context(segment, sizeof(segment), true);
    int cols = terminal_columns();
    if (cols > 0) {
        int budget = cols - 1;
        int left_width = visible_width(left) + (left[0] ? 3 : 2);
        if (left_width + visible_width(segment) > budget) {
            format_context(segment, sizeof(segment), false);
        }
        if (left_width + visible_width(segment) > budget) {
            segment[0] = '\0';
        }
    }

    bool printed = true;
    if (left[0] && segment[0]) {
        fprintf(stdout, "%s   %s\n", left, segment);
    } else if (left[0]) {
        fprintf(stdout, "%s\n", left);
    } else if (segment[0]) {
        fprintf(stdout, "  %s\n", segment);
    } else {
        printed = false;
    }

    g_info_rendered = printed;
    fflush(stdout);
}

void status_line_clear_rendered(void) {
    if (!g_status.initialized) return;
    if (get_json_output_mode()) return;
    if (!g_info_rendered) return;

    fprintf(stdout, "\033[A" TERM_CLEAR_LINE);
    fflush(stdout);
    g_info_rendered = false;
}

char *status_line_build_prompt(void) {
    if (get_json_output_mode()) {
        return strdup("> ");
    }

    PromptMode mode = PROMPT_MODE_DEFAULT;
    if (g_status.initialized) {
        pthread_mutex_lock(&g_status.mutex);
        mode = g_status.current_mode;
        pthread_mutex_unlock(&g_status.mutex);
    }

    if (mode != PROMPT_MODE_DEFAULT) {
        const char* name = prompt_mode_name(mode);
        char buf[128];
        snprintf(buf, sizeof(buf),
                 RL_START "\033[2m" RL_END "[%s]"
                 RL_START "\033[0m" RL_END " "
                 RL_START "\033[1m" RL_END "> "
                 RL_START "\033[0m" RL_END,
                 name);
        return strdup(buf);
    }

    return strdup(RL_START "\033[1m" RL_END "> " RL_START "\033[0m" RL_END);
}
