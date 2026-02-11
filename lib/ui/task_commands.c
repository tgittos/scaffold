#include "task_commands.h"
#include "terminal.h"
#include "../agent/session.h"
#include "../services/services.h"
#include "../db/task_store.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *GLOBAL_SESSION_ID = "global";

static const char *status_color(TaskStatus status) {
    switch (status) {
        case TASK_STATUS_IN_PROGRESS: return TERM_CYAN;
        case TASK_STATUS_PENDING:     return TERM_YELLOW;
        case TASK_STATUS_COMPLETED:   return TERM_DIM;
        default:                      return TERM_RESET;
    }
}

static const char *priority_label(TaskPriority p) {
    switch (p) {
        case TASK_PRIORITY_HIGH:   return TERM_RED "high" TERM_RESET;
        case TASK_PRIORITY_MEDIUM: return TERM_YELLOW "med" TERM_RESET;
        case TASK_PRIORITY_LOW:    return TERM_DIM "low" TERM_RESET;
        default:                   return "";
    }
}

static void truncate_display(const char *text, char *buf, size_t buf_size, size_t max_chars) {
    if (text == NULL) {
        buf[0] = '\0';
        return;
    }
    size_t len = strlen(text);
    if (len <= max_chars) {
        snprintf(buf, buf_size, "%s", text);
    } else {
        snprintf(buf, buf_size, "%.*s...", (int)(max_chars - 3), text);
    }
}

static int cmd_tasks_list(AgentSession *session) {
    task_store_t *store = services_get_task_store(session->services);
    if (store == NULL) {
        printf(TERM_DIM "  No task store available.\n" TERM_RESET);
        return 0;
    }

    size_t count = 0;
    Task **tasks = task_store_list_by_session(store, GLOBAL_SESSION_ID, -1, &count);
    if (tasks == NULL || count == 0) {
        printf(TERM_DIM "  No tasks.\n" TERM_RESET);
        if (tasks) task_free_list(tasks, count);
        return 0;
    }

    printf("\n" TERM_BOLD "Tasks" TERM_RESET " (%zu)\n", count);
    printf(TERM_SEP_LIGHT_40 "\n");

    /* Display grouped by status: in_progress, pending, completed */
    TaskStatus order[] = { TASK_STATUS_IN_PROGRESS, TASK_STATUS_PENDING, TASK_STATUS_COMPLETED };
    for (int g = 0; g < 3; g++) {
        for (size_t i = 0; i < count; i++) {
            if (tasks[i]->status != order[g]) continue;

            char trunc[60];
            truncate_display(tasks[i]->content, trunc, sizeof(trunc), 50);

            printf("  %s%.8s" TERM_RESET "  %s%-12s" TERM_RESET "  [%s]  %s\n",
                   status_color(tasks[i]->status), tasks[i]->id,
                   status_color(tasks[i]->status), task_status_to_string(tasks[i]->status),
                   priority_label(tasks[i]->priority),
                   trunc);
        }
    }
    printf("\n");

    task_free_list(tasks, count);
    return 0;
}

static int cmd_tasks_ready(AgentSession *session) {
    task_store_t *store = services_get_task_store(session->services);
    if (store == NULL) {
        printf(TERM_DIM "  No task store available.\n" TERM_RESET);
        return 0;
    }

    size_t count = 0;
    Task **tasks = task_store_list_ready(store, GLOBAL_SESSION_ID, &count);
    if (tasks == NULL || count == 0) {
        printf(TERM_DIM "  No ready tasks.\n" TERM_RESET);
        if (tasks) task_free_list(tasks, count);
        return 0;
    }

    printf("\n" TERM_BOLD "Ready Tasks" TERM_RESET " (%zu)\n", count);
    printf(TERM_SEP_LIGHT_40 "\n");

    for (size_t i = 0; i < count; i++) {
        char trunc[60];
        truncate_display(tasks[i]->content, trunc, sizeof(trunc), 50);
        printf("  %.8s  [%s]  %s\n",
               tasks[i]->id, priority_label(tasks[i]->priority), trunc);
    }
    printf("\n");

    task_free_list(tasks, count);
    return 0;
}

static int cmd_tasks_show(const char *id_prefix, AgentSession *session) {
    task_store_t *store = services_get_task_store(session->services);
    if (store == NULL) {
        printf(TERM_DIM "  No task store available.\n" TERM_RESET);
        return 0;
    }

    /* Try exact match first, then prefix search among session tasks */
    Task *task = task_store_get_task(store, id_prefix);
    if (task == NULL) {
        size_t count = 0;
        Task **all = task_store_list_by_session(store, GLOBAL_SESSION_ID, -1, &count);
        if (all != NULL) {
            size_t prefix_len = strlen(id_prefix);
            for (size_t i = 0; i < count; i++) {
                if (strncmp(all[i]->id, id_prefix, prefix_len) == 0) {
                    task = task_store_get_task(store, all[i]->id);
                    break;
                }
            }
            task_free_list(all, count);
        }
    }

    if (task == NULL) {
        printf("  Task not found: %s\n", id_prefix);
        return 0;
    }

    printf("\n" TERM_BOLD "Task %.8s" TERM_RESET "\n", task->id);
    printf(TERM_SEP_LIGHT_40 "\n");
    printf("  " TERM_BOLD "ID:       " TERM_RESET "%s\n", task->id);
    printf("  " TERM_BOLD "Status:   " TERM_RESET "%s%s" TERM_RESET "\n",
           status_color(task->status), task_status_to_string(task->status));
    printf("  " TERM_BOLD "Priority: " TERM_RESET "%s\n", priority_label(task->priority));
    if (task->content) {
        printf("  " TERM_BOLD "Content:  " TERM_RESET "%s\n", task->content);
    }
    if (task->parent_id[0]) {
        printf("  " TERM_BOLD "Parent:   " TERM_RESET "%.8s\n", task->parent_id);
    }

    size_t blocker_count = 0;
    char **blockers = task_store_get_blockers(store, task->id, &blocker_count);
    if (blockers != NULL && blocker_count > 0) {
        printf("  " TERM_BOLD "Blocked by:" TERM_RESET);
        for (size_t i = 0; i < blocker_count; i++) {
            printf(" %.8s", blockers[i]);
        }
        printf("\n");
    }
    if (blockers) task_free_id_list(blockers, blocker_count);

    size_t blocking_count = 0;
    char **blocking = task_store_get_blocking(store, task->id, &blocking_count);
    if (blocking != NULL && blocking_count > 0) {
        printf("  " TERM_BOLD "Blocks:   " TERM_RESET);
        for (size_t i = 0; i < blocking_count; i++) {
            printf(" %.8s", blocking[i]);
        }
        printf("\n");
    }
    if (blocking) task_free_id_list(blocking, blocking_count);

    printf("\n");
    task_free(task);
    return 0;
}

static void print_task_help(void) {
    printf("\n" TERM_BOLD "Task Commands" TERM_RESET "\n");
    printf(TERM_SEP_LIGHT_40 "\n");
    printf("  " TERM_BOLD "/tasks" TERM_RESET "              List all tasks\n");
    printf("  " TERM_BOLD "/tasks ready" TERM_RESET "        Show unblocked pending tasks\n");
    printf("  " TERM_BOLD "/tasks show <id>" TERM_RESET "    Show task details (prefix match)\n");
    printf("  " TERM_BOLD "/tasks help" TERM_RESET "         Show this help\n\n");
}

int process_task_command(const char *args, AgentSession *session) {
    if (args == NULL || args[0] == '\0' || strcmp(args, "list") == 0) {
        return cmd_tasks_list(session);
    }
    if (strcmp(args, "ready") == 0) {
        return cmd_tasks_ready(session);
    }
    if (strcmp(args, "help") == 0) {
        print_task_help();
        return 0;
    }
    if (strncmp(args, "show ", 5) == 0) {
        const char *id = args + 5;
        while (*id == ' ') id++;
        if (*id == '\0') {
            printf("  Usage: /tasks show <id>\n");
            return 0;
        }
        return cmd_tasks_show(id, session);
    }
    printf("  Unknown subcommand: %s\n", args);
    print_task_help();
    return 0;
}
