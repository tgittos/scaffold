#include "agent_commands.h"
#include "terminal.h"
#include "../agent/session.h"
#include "../services/services.h"
#include "../db/goal_store.h"
#include "../tools/subagent_tool.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *agent_status_color(SubagentStatus status) {
    switch (status) {
        case SUBAGENT_STATUS_RUNNING:   return TERM_CYAN;
        case SUBAGENT_STATUS_COMPLETED: return TERM_GREEN;
        case SUBAGENT_STATUS_FAILED:    return TERM_RED;
        case SUBAGENT_STATUS_TIMEOUT:   return TERM_YELLOW;
        case SUBAGENT_STATUS_PENDING:   return TERM_DIM;
        default:                        return TERM_RESET;
    }
}

static void format_elapsed(time_t start, char *buf, size_t buf_size) {
    if (start == 0) {
        snprintf(buf, buf_size, "--");
        return;
    }
    int elapsed = (int)(time(NULL) - start);
    if (elapsed < 60) {
        snprintf(buf, buf_size, "%ds", elapsed);
    } else if (elapsed < 3600) {
        snprintf(buf, buf_size, "%dm%ds", elapsed / 60, elapsed % 60);
    } else {
        snprintf(buf, buf_size, "%dh%dm", elapsed / 3600, (elapsed % 3600) / 60);
    }
}

static void format_elapsed_millis(int64_t started_at_ms, char *buf, size_t buf_size) {
    if (started_at_ms <= 0) {
        snprintf(buf, buf_size, "--");
        return;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t now_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    int elapsed = (int)((now_ms - started_at_ms) / 1000);
    if (elapsed < 0) elapsed = 0;

    if (elapsed < 60) {
        snprintf(buf, buf_size, "%ds", elapsed);
    } else if (elapsed < 3600) {
        snprintf(buf, buf_size, "%dm%ds", elapsed / 60, elapsed % 60);
    } else {
        snprintf(buf, buf_size, "%dh%dm", elapsed / 3600, (elapsed % 3600) / 60);
    }
}

static void print_supervisors(AgentSession *session) {
    if (session->services == NULL) return;
    goal_store_t *store = services_get_goal_store(session->services);
    if (store == NULL) return;

    size_t count = 0;
    Goal **goals = goal_store_list_all(store, &count);
    if (goals == NULL || count == 0) {
        if (goals) goal_free_list(goals, count);
        return;
    }

    /* Count supervisors */
    int sup_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (goals[i]->supervisor_pid > 0) sup_count++;
    }

    if (sup_count == 0) {
        goal_free_list(goals, count);
        return;
    }

    printf("\n" TERM_BOLD "Supervisors" TERM_RESET " (%d)\n", sup_count);
    printf(TERM_SEP_LIGHT_40 "\n");

    for (size_t i = 0; i < count; i++) {
        Goal *g = goals[i];
        if (g->supervisor_pid <= 0) continue;

        char elapsed[16];
        format_elapsed_millis(g->supervisor_started_at, elapsed, sizeof(elapsed));

        char name_trunc[35];
        size_t nlen = strlen(g->name);
        if (nlen > 30) {
            snprintf(name_trunc, sizeof(name_trunc), "%.27s...", g->name);
        } else {
            snprintf(name_trunc, sizeof(name_trunc), "%s", g->name);
        }

        printf("  %.8s  " TERM_CYAN "pid %-6d" TERM_RESET "  %5s  %s\n",
               g->id, g->supervisor_pid, elapsed, name_trunc);
    }

    goal_free_list(goals, count);
}

static int cmd_agents_list(AgentSession *session) {
    print_supervisors(session);

    SubagentManager *mgr = &session->subagent_manager;
    subagent_poll_all(mgr);

    size_t count = mgr->subagents.count;
    if (count == 0) {
        printf(TERM_DIM "  No subagents.\n" TERM_RESET);
        return 0;
    }

    int running = 0, completed = 0, failed = 0;
    for (size_t i = 0; i < count; i++) {
        switch (mgr->subagents.data[i].status) {
            case SUBAGENT_STATUS_RUNNING:   running++;   break;
            case SUBAGENT_STATUS_COMPLETED: completed++; break;
            case SUBAGENT_STATUS_FAILED:
            case SUBAGENT_STATUS_TIMEOUT:   failed++;    break;
            default: break;
        }
    }

    printf("\n" TERM_BOLD "Subagents" TERM_RESET " (%zu)", count);
    printf("  " TERM_CYAN "%d running" TERM_RESET, running);
    if (completed > 0) printf("  " TERM_GREEN "%d done" TERM_RESET, completed);
    if (failed > 0)    printf("  " TERM_RED "%d failed" TERM_RESET, failed);
    printf("\n" TERM_SEP_LIGHT_40 "\n");

    for (size_t i = 0; i < count; i++) {
        Subagent *sub = &mgr->subagents.data[i];
        const char *status_str = subagent_status_to_string(sub->status);
        char elapsed[16];
        format_elapsed(sub->start_time, elapsed, sizeof(elapsed));

        char task_trunc[50];
        if (sub->task) {
            size_t len = strlen(sub->task);
            if (len > 45) {
                snprintf(task_trunc, sizeof(task_trunc), "%.42s...", sub->task);
            } else {
                snprintf(task_trunc, sizeof(task_trunc), "%s", sub->task);
            }
        } else {
            snprintf(task_trunc, sizeof(task_trunc), "(no task)");
        }

        printf("  %.8s  %s%-10s" TERM_RESET "  %5s  %s\n",
               sub->id,
               agent_status_color(sub->status), status_str,
               elapsed, task_trunc);
    }
    printf("\n");
    return 0;
}

static int cmd_agents_show(const char *id_prefix, AgentSession *session) {
    SubagentManager *mgr = &session->subagent_manager;
    subagent_poll_all(mgr);

    /* Prefix match */
    Subagent *found = NULL;
    size_t prefix_len = strlen(id_prefix);
    for (size_t i = 0; i < mgr->subagents.count; i++) {
        if (strncmp(mgr->subagents.data[i].id, id_prefix, prefix_len) == 0) {
            found = &mgr->subagents.data[i];
            break;
        }
    }

    if (found == NULL) {
        printf("  Subagent not found: %s\n", id_prefix);
        return 0;
    }

    char elapsed[16];
    format_elapsed(found->start_time, elapsed, sizeof(elapsed));

    printf("\n" TERM_BOLD "Subagent %.8s" TERM_RESET "\n", found->id);
    printf(TERM_SEP_LIGHT_40 "\n");
    printf("  " TERM_BOLD "ID:      " TERM_RESET "%s\n", found->id);
    printf("  " TERM_BOLD "Status:  " TERM_RESET "%s%s" TERM_RESET "\n",
           agent_status_color(found->status), subagent_status_to_string(found->status));
    printf("  " TERM_BOLD "Elapsed: " TERM_RESET "%s\n", elapsed);
    if (found->task) {
        printf("  " TERM_BOLD "Task:    " TERM_RESET "%s\n", found->task);
    }
    if (found->result) {
        printf("  " TERM_BOLD "Result:  " TERM_RESET "%.200s%s\n",
               found->result, strlen(found->result) > 200 ? "..." : "");
    }
    if (found->error) {
        printf("  " TERM_BOLD "Error:   " TERM_RESET TERM_RED "%s" TERM_RESET "\n", found->error);
    }
    if (found->output && found->output_len > 0) {
        /* Show last 200 chars of output */
        size_t show_start = found->output_len > 200 ? found->output_len - 200 : 0;
        printf("  " TERM_BOLD "Output:  " TERM_RESET "%s%.200s\n",
               show_start > 0 ? "..." : "", found->output + show_start);
    }
    printf("\n");
    return 0;
}

static void print_agent_help(void) {
    printf("\n" TERM_BOLD "Agent Commands" TERM_RESET "\n");
    printf(TERM_SEP_LIGHT_40 "\n");
    printf("  " TERM_BOLD "/agents" TERM_RESET "             List supervisors and subagents\n");
    printf("  " TERM_BOLD "/agents show <id>" TERM_RESET "   Show subagent details (prefix match)\n");
    printf("  " TERM_BOLD "/agents help" TERM_RESET "        Show this help\n\n");
}

int process_agent_command(const char *args, AgentSession *session) {
    if (session == NULL) return -1;

    if (args == NULL || args[0] == '\0' || strcmp(args, "list") == 0) {
        return cmd_agents_list(session);
    }
    if (strcmp(args, "help") == 0) {
        print_agent_help();
        return 0;
    }
    if (strncmp(args, "show ", 5) == 0) {
        const char *id = args + 5;
        while (*id == ' ') id++;
        if (*id == '\0') {
            printf("  Usage: /agents show <id>\n");
            return 0;
        }
        return cmd_agents_show(id, session);
    }
    printf("  Unknown subcommand: %s\n", args);
    print_agent_help();
    return 0;
}
