#include "agent_commands.h"
#include "terminal.h"
#include "../agent/session.h"
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

static int cmd_agents_list(AgentSession *session) {
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
    printf("  " TERM_BOLD "/agents" TERM_RESET "             List all subagents\n");
    printf("  " TERM_BOLD "/agents show <id>" TERM_RESET "   Show subagent details (prefix match)\n");
    printf("  " TERM_BOLD "/agents help" TERM_RESET "        Show this help\n\n");
}

int process_agent_command(const char *args, AgentSession *session) {
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
