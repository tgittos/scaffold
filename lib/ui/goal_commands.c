#include "goal_commands.h"
#include "terminal.h"
#include "../agent/session.h"
#include "../services/services.h"
#include "../db/goal_store.h"
#include "../db/action_store.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *goal_status_color(GoalStatus status) {
    switch (status) {
        case GOAL_STATUS_ACTIVE:    return TERM_CYAN;
        case GOAL_STATUS_COMPLETED: return TERM_GREEN;
        case GOAL_STATUS_FAILED:    return TERM_RED;
        case GOAL_STATUS_PAUSED:    return TERM_YELLOW;
        case GOAL_STATUS_PLANNING:  return TERM_DIM;
        default:                    return TERM_RESET;
    }
}

static const char *action_status_color(ActionStatus status) {
    switch (status) {
        case ACTION_STATUS_RUNNING:   return TERM_CYAN;
        case ACTION_STATUS_COMPLETED: return TERM_GREEN;
        case ACTION_STATUS_FAILED:    return TERM_RED;
        case ACTION_STATUS_SKIPPED:   return TERM_DIM;
        case ACTION_STATUS_PENDING:   return TERM_YELLOW;
        default:                      return TERM_RESET;
    }
}

static const char *action_status_symbol(ActionStatus status) {
    switch (status) {
        case ACTION_STATUS_RUNNING:   return TERM_SYM_ACTIVE;
        case ACTION_STATUS_COMPLETED: return TERM_SYM_SUCCESS;
        case ACTION_STATUS_FAILED:    return TERM_SYM_ERROR;
        case ACTION_STATUS_SKIPPED:   return TERM_DIM "-" TERM_RESET;
        case ACTION_STATUS_PENDING:   return TERM_SYM_INFO;
        default:                      return " ";
    }
}

/* Count goal_state keys satisfied in world_state (world_state has key set to true) */
static void count_progress(const char *goal_state_json, const char *world_state_json,
                           int *total, int *satisfied) {
    *total = 0;
    *satisfied = 0;
    if (goal_state_json == NULL || goal_state_json[0] == '\0') return;

    cJSON *gs = cJSON_Parse(goal_state_json);
    if (gs == NULL) return;

    cJSON *ws = (world_state_json && world_state_json[0])
                ? cJSON_Parse(world_state_json) : NULL;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, gs) {
        (*total)++;
        cJSON *ws_val = ws ? cJSON_GetObjectItem(ws, item->string) : NULL;
        if (ws_val && cJSON_IsTrue(ws_val)) {
            (*satisfied)++;
        }
    }

    if (ws) cJSON_Delete(ws);
    cJSON_Delete(gs);
}

static void format_elapsed_since(int64_t started_at_ms, char *buf, size_t buf_size) {
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

static int cmd_goals_list(AgentSession *session) {
    goal_store_t *store = services_get_goal_store(session->services);
    if (store == NULL) {
        printf(TERM_DIM "  No goal store available.\n" TERM_RESET);
        return 0;
    }

    size_t count = 0;
    Goal **goals = goal_store_list_all(store, &count);
    if (goals == NULL || count == 0) {
        printf(TERM_DIM "  No goals.\n" TERM_RESET);
        if (goals) goal_free_list(goals, count);
        return 0;
    }

    printf("\n" TERM_BOLD "Goals" TERM_RESET " (%zu)\n", count);
    printf(TERM_SEP_LIGHT_40 "\n");

    for (size_t i = 0; i < count; i++) {
        Goal *g = goals[i];
        int total = 0, satisfied = 0;
        count_progress(g->goal_state, g->world_state, &total, &satisfied);

        char progress[32];
        if (total > 0) {
            snprintf(progress, sizeof(progress), "%d/%d", satisfied, total);
        } else {
            snprintf(progress, sizeof(progress), "--");
        }

        char name_trunc[40];
        size_t nlen = strlen(g->name);
        if (nlen > 35) {
            snprintf(name_trunc, sizeof(name_trunc), "%.32s...", g->name);
        } else {
            snprintf(name_trunc, sizeof(name_trunc), "%s", g->name);
        }

        const char *sup_indicator = "";
        if (g->supervisor_pid > 0) {
            sup_indicator = TERM_DIM " [sup]" TERM_RESET;
        }

        printf("  %.8s  %s%-10s" TERM_RESET "  %5s  %s%s\n",
               g->id,
               goal_status_color(g->status), goal_status_to_string(g->status),
               progress, name_trunc, sup_indicator);
    }
    printf("\n");

    goal_free_list(goals, count);
    return 0;
}

static void print_action_tree(action_store_t *store, const char *parent_id,
                               int indent) {
    size_t count = 0;
    Action **actions = action_store_list_children(store, parent_id, &count);
    if (actions == NULL || count == 0) {
        if (actions) action_free_list(actions, count);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        Action *a = actions[i];
        bool is_last = (i == count - 1);

        /* Print indent guides for nested levels */
        for (int d = 0; d < indent; d++) {
            printf("  " TERM_DIM TERM_BOX_LIGHT_V TERM_RESET " ");
        }

        const char *connector = is_last ? TERM_TREE_LAST : TERM_TREE_BRANCH;
        const char *type_label = a->is_compound ? TERM_BOLD "+" TERM_RESET : " ";

        char desc_trunc[50];
        if (a->description) {
            size_t dlen = strlen(a->description);
            if (dlen > 45) {
                snprintf(desc_trunc, sizeof(desc_trunc), "%.42s...", a->description);
            } else {
                snprintf(desc_trunc, sizeof(desc_trunc), "%s", a->description);
            }
        } else {
            snprintf(desc_trunc, sizeof(desc_trunc), "(no description)");
        }

        printf("%s %s%s" TERM_RESET " %s%s\n",
               connector,
               action_status_color(a->status), action_status_symbol(a->status),
               type_label, desc_trunc);

        if (a->is_compound) {
            print_action_tree(store, a->id, indent + 1);
        }
    }

    action_free_list(actions, count);
}

static int cmd_goals_show(const char *id_prefix, AgentSession *session) {
    goal_store_t *gstore = services_get_goal_store(session->services);
    action_store_t *astore = services_get_action_store(session->services);
    if (gstore == NULL) {
        printf(TERM_DIM "  No goal store available.\n" TERM_RESET);
        return 0;
    }

    /* Try exact match first, then prefix search */
    Goal *goal = goal_store_get(gstore, id_prefix);
    if (goal == NULL) {
        size_t count = 0;
        Goal **all = goal_store_list_all(gstore, &count);
        if (all != NULL) {
            size_t prefix_len = strlen(id_prefix);
            for (size_t i = 0; i < count; i++) {
                if (strncmp(all[i]->id, id_prefix, prefix_len) == 0) {
                    goal = goal_store_get(gstore, all[i]->id);
                    break;
                }
            }
            goal_free_list(all, count);
        }
    }

    if (goal == NULL) {
        printf("  Goal not found: %s\n", id_prefix);
        return 0;
    }

    int total = 0, satisfied = 0;
    count_progress(goal->goal_state, goal->world_state, &total, &satisfied);

    printf("\n" TERM_BOLD "Goal %.8s" TERM_RESET "\n", goal->id);
    printf(TERM_SEP_LIGHT_40 "\n");
    printf("  " TERM_BOLD "Name:     " TERM_RESET "%s\n", goal->name);
    printf("  " TERM_BOLD "Status:   " TERM_RESET "%s%s" TERM_RESET "\n",
           goal_status_color(goal->status), goal_status_to_string(goal->status));
    if (total > 0) {
        printf("  " TERM_BOLD "Progress: " TERM_RESET "%d/%d assertions\n",
               satisfied, total);
    }
    if (goal->description && goal->description[0]) {
        char desc_trunc[80];
        size_t dlen = strlen(goal->description);
        if (dlen > 75) {
            snprintf(desc_trunc, sizeof(desc_trunc), "%.72s...", goal->description);
        } else {
            snprintf(desc_trunc, sizeof(desc_trunc), "%s", goal->description);
        }
        printf("  " TERM_BOLD "Desc:     " TERM_RESET "%s\n", desc_trunc);
    }
    if (goal->supervisor_pid > 0) {
        char elapsed[16];
        format_elapsed_since(goal->supervisor_started_at, elapsed, sizeof(elapsed));
        printf("  " TERM_BOLD "Super:    " TERM_RESET "pid %d (%s)\n",
               goal->supervisor_pid, elapsed);
    }
    if (goal->summary && goal->summary[0]) {
        printf("  " TERM_BOLD "Summary:  " TERM_RESET "%s\n", goal->summary);
    }

    /* World state progress */
    if (goal->world_state && goal->world_state[0]) {
        cJSON *ws = cJSON_Parse(goal->world_state);
        if (ws != NULL) {
            int ws_count = cJSON_GetArraySize(ws);
            if (ws_count > 0) {
                printf("\n  " TERM_BOLD "World State:" TERM_RESET "\n");
                cJSON *item = NULL;
                cJSON_ArrayForEach(item, ws) {
                    const char *sym = cJSON_IsTrue(item)
                        ? TERM_GREEN TERM_SYM_SUCCESS TERM_RESET
                        : TERM_DIM TERM_SYM_INFO TERM_RESET;
                    printf("    %s %s\n", sym, item->string);
                }
            }
            cJSON_Delete(ws);
        }
    }

    /* Action tree */
    if (astore != NULL) {
        size_t action_count = 0;
        Action **actions = action_store_list_by_goal(astore, goal->id, &action_count);
        if (actions != NULL && action_count > 0) {
            /* Count by status */
            int pending = 0, running = 0, completed = 0, failed = 0;
            for (size_t i = 0; i < action_count; i++) {
                switch (actions[i]->status) {
                    case ACTION_STATUS_PENDING:   pending++;   break;
                    case ACTION_STATUS_RUNNING:    running++;   break;
                    case ACTION_STATUS_COMPLETED:  completed++; break;
                    case ACTION_STATUS_FAILED:     failed++;    break;
                    default: break;
                }
            }

            printf("\n  " TERM_BOLD "Actions" TERM_RESET " (%zu)", action_count);
            if (running > 0)   printf("  " TERM_CYAN "%d running" TERM_RESET, running);
            if (completed > 0) printf("  " TERM_GREEN "%d done" TERM_RESET, completed);
            if (pending > 0)   printf("  " TERM_YELLOW "%d pending" TERM_RESET, pending);
            if (failed > 0)    printf("  " TERM_RED "%d failed" TERM_RESET, failed);
            printf("\n");

            /* Print top-level actions (no parent) as tree roots */
            for (size_t i = 0; i < action_count; i++) {
                Action *a = actions[i];
                if (a->parent_action_id[0] != '\0') continue;

                bool is_last_top = true;
                for (size_t j = i + 1; j < action_count; j++) {
                    if (actions[j]->parent_action_id[0] == '\0') {
                        is_last_top = false;
                        break;
                    }
                }

                const char *connector = is_last_top ? TERM_TREE_LAST : TERM_TREE_BRANCH;
                const char *type_label = a->is_compound ? TERM_BOLD "+" TERM_RESET : " ";

                char desc_trunc[50];
                if (a->description) {
                    size_t dlen = strlen(a->description);
                    if (dlen > 45) {
                        snprintf(desc_trunc, sizeof(desc_trunc), "%.42s...", a->description);
                    } else {
                        snprintf(desc_trunc, sizeof(desc_trunc), "%s", a->description);
                    }
                } else {
                    snprintf(desc_trunc, sizeof(desc_trunc), "(no description)");
                }

                printf("  %s %s%s" TERM_RESET " %s%s",
                       connector,
                       action_status_color(a->status), action_status_symbol(a->status),
                       type_label, desc_trunc);

                if (a->role[0] && strcmp(a->role, "implementation") != 0) {
                    printf(TERM_DIM " [%s]" TERM_RESET, a->role);
                }
                printf("\n");

                if (a->is_compound) {
                    print_action_tree(astore, a->id, 1);
                }
            }
        }
        if (actions) action_free_list(actions, action_count);
    }

    printf("\n");
    goal_free(goal);
    return 0;
}

static void print_goals_help(void) {
    printf("\n" TERM_BOLD "Goal Commands" TERM_RESET "\n");
    printf(TERM_SEP_LIGHT_40 "\n");
    printf("  " TERM_BOLD "/goals" TERM_RESET "              List all goals\n");
    printf("  " TERM_BOLD "/goals show <id>" TERM_RESET "    Show goal details + action tree (prefix match)\n");
    printf("  " TERM_BOLD "/goals help" TERM_RESET "         Show this help\n\n");
}

int process_goals_command(const char *args, AgentSession *session) {
    if (session == NULL) return -1;

    if (args == NULL || args[0] == '\0' || strcmp(args, "list") == 0) {
        return cmd_goals_list(session);
    }
    if (strcmp(args, "help") == 0) {
        print_goals_help();
        return 0;
    }
    if (strncmp(args, "show ", 5) == 0) {
        const char *id = args + 5;
        while (*id == ' ') id++;
        if (*id == '\0') {
            printf("  Usage: /goals show <id>\n");
            return 0;
        }
        return cmd_goals_show(id, session);
    }

    /* Treat bare argument as goal ID shorthand for /goals show <id> */
    return cmd_goals_show(args, session);
}
