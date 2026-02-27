#include "orchestrator_tool.h"
#include "tool_param_dsl.h"
#include "tool_result_builder.h"
#include "../util/common_utils.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "services/services.h"
#include "orchestrator/orchestrator.h"
#include "orchestrator/goap_state.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Services *g_services = NULL;

void orchestrator_tool_set_services(Services *services) {
    g_services = services;
}

/* ========================================================================
 * Parameter definitions
 * ======================================================================== */

static const ParamDef EXECUTE_PLAN_PARAMS[] = {
    {"plan_text", "string", "The complete plan text to decompose into goals and actions", NULL, 1},
};

static const ParamDef GOAL_ID_PARAMS[] = {
    {"goal_id", "string", "ID of the goal", NULL, 1},
};

/* ========================================================================
 * Tool definitions table
 * ======================================================================== */

static const ToolDef ORCHESTRATOR_TOOLS[] = {
    {"execute_plan",
     "Decompose a finalized plan into GOAP goals and actions. Returns the plan with decomposition instructions. After calling this, use goap_create_goal and goap_create_actions to create the goal hierarchy, then call start_goal for each goal.",
     EXECUTE_PLAN_PARAMS, 1, execute_execute_plan},
    {"list_goals",
     "List all goals with their status and world state progress",
     NULL, 0, execute_list_goals},
    {"goal_status",
     "Get detailed status for a goal: world state, action tree, supervisor info",
     GOAL_ID_PARAMS, 1, execute_goal_status},
    {"start_goal",
     "Activate a goal and spawn its supervisor process. Call after creating the goal and its initial actions.",
     GOAL_ID_PARAMS, 1, execute_start_goal},
    {"pause_goal",
     "Pause a goal by stopping its supervisor. The goal can be resumed later with start_goal.",
     GOAL_ID_PARAMS, 1, execute_pause_goal},
    {"cancel_goal",
     "Cancel a goal by killing its supervisor and marking it failed",
     GOAL_ID_PARAMS, 1, execute_cancel_goal},
};

#define ORCHESTRATOR_TOOL_COUNT (sizeof(ORCHESTRATOR_TOOLS) / sizeof(ORCHESTRATOR_TOOLS[0]))

int register_orchestrator_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    int registered = register_tools_from_defs(registry, ORCHESTRATOR_TOOLS,
                                              ORCHESTRATOR_TOOL_COUNT);
    return (registered == (int)ORCHESTRATOR_TOOL_COUNT) ? 0 : -1;
}

/* ========================================================================
 * execute_plan
 * ======================================================================== */

static const char DECOMPOSITION_INSTRUCTION[] =
    "You are now in DECOMPOSITION MODE. Your task is to break down the plan below "
    "into GOAP goals and actions.\n\n"
    "For each major objective in the plan:\n"
    "1. Create a goal using goap_create_goal with:\n"
    "   - A short descriptive name\n"
    "   - A description of what the goal achieves\n"
    "   - goal_state: JSON object with boolean assertion keys that define completion\n"
    "2. Create initial compound actions for each goal using goap_create_actions:\n"
    "   - 3-5 high-level phases as compound actions (is_compound: true)\n"
    "   - Each with preconditions (what must be true first) and effects (what becomes true)\n"
    "   - Include verification phases (code_review, testing) alongside implementation\n"
    "   - Preconditions create ordering: an action waits until its preconditions are in world_state\n"
    "3. After creating each goal and its initial actions, call start_goal to begin execution.\n\n"
    "PLAN TO DECOMPOSE:\n";

int execute_execute_plan(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *plan_text = extract_string_param(tc->arguments, "plan_text");
    if (!plan_text) {
        tool_result_set_error(result, "Missing required parameter: plan_text");
        return 0;
    }

    size_t inst_len = strlen(DECOMPOSITION_INSTRUCTION);
    size_t plan_len = strlen(plan_text);
    char *response = malloc(inst_len + plan_len + 1);
    if (!response) {
        tool_result_set_error(result, "Memory allocation failed");
        free(plan_text);
        return 0;
    }

    memcpy(response, DECOMPOSITION_INSTRUCTION, inst_len);
    memcpy(response + inst_len, plan_text, plan_len);
    response[inst_len + plan_len] = '\0';

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON_AddStringToObject(json, "instruction", response);

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;
    result->clear_history = 1;

    cJSON_Delete(json);
    free(response);
    free(plan_text);
    return 0;
}

/* ========================================================================
 * list_goals
 * ======================================================================== */

int execute_list_goals(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    goal_store_t *gs = services_get_goal_store(g_services);
    if (!gs) {
        tool_result_set_error(result, "Goal store not available");
        return 0;
    }

    size_t count = 0;
    Goal **goals = goal_store_list_all(gs, &count);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON *arr = cJSON_CreateArray();

    for (size_t i = 0; i < count; i++) {
        cJSON *g = cJSON_CreateObject();
        cJSON_AddStringToObject(g, "id", goals[i]->id);
        cJSON_AddStringToObject(g, "name", goals[i]->name);
        cJSON_AddStringToObject(g, "status", goal_status_to_string(goals[i]->status));

        GoapProgress gp = goap_check_progress(goals[i]->goal_state,
                                               goals[i]->world_state);
        char progress[32];
        snprintf(progress, sizeof(progress), "%d/%d", gp.satisfied, gp.total);
        cJSON_AddStringToObject(g, "progress", progress);

        if (goals[i]->summary)
            cJSON_AddStringToObject(g, "summary", goals[i]->summary);

        cJSON_AddBoolToObject(g, "supervisor_running",
                              goals[i]->supervisor_pid > 0 ? cJSON_True : cJSON_False);

        cJSON_AddItemToArray(arr, g);
    }

    cJSON_AddItemToObject(json, "goals", arr);
    cJSON_AddNumberToObject(json, "count", (double)count);

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;

    cJSON_Delete(json);
    goal_free_list(goals, count);
    return 0;
}

/* ========================================================================
 * goal_status
 * ======================================================================== */

static cJSON *build_action_summary(const Action *a) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", a->id);
    if (a->description)
        cJSON_AddStringToObject(obj, "description", a->description);
    cJSON_AddStringToObject(obj, "status", action_status_to_string(a->status));
    cJSON_AddBoolToObject(obj, "is_compound", a->is_compound);
    if (a->role[0])
        cJSON_AddStringToObject(obj, "role", a->role);
    if (a->effects) {
        cJSON *e = cJSON_Parse(a->effects);
        if (e) cJSON_AddItemToObject(obj, "effects", e);
    }
    if (a->result) {
        size_t rlen = strlen(a->result);
        if (rlen > 200) {
            char preview[220];
            snprintf(preview, sizeof(preview), "%.200s...", a->result);
            cJSON_AddStringToObject(obj, "result_preview", preview);
        } else {
            cJSON_AddStringToObject(obj, "result_preview", a->result);
        }
    }
    return obj;
}

static void build_action_tree(cJSON *parent_arr, Action **all_actions,
                               size_t count, const char *parent_id) {
    for (size_t i = 0; i < count; i++) {
        bool matches;
        if (parent_id == NULL || parent_id[0] == '\0') {
            matches = (all_actions[i]->parent_action_id[0] == '\0');
        } else {
            matches = (strcmp(all_actions[i]->parent_action_id, parent_id) == 0);
        }
        if (!matches) continue;

        cJSON *node = build_action_summary(all_actions[i]);
        if (all_actions[i]->is_compound) {
            cJSON *children = cJSON_CreateArray();
            build_action_tree(children, all_actions, count, all_actions[i]->id);
            if (cJSON_GetArraySize(children) > 0) {
                cJSON_AddItemToObject(node, "children", children);
            } else {
                cJSON_Delete(children);
            }
        }
        cJSON_AddItemToArray(parent_arr, node);
    }
}

int execute_goal_status(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        tool_result_set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    goal_store_t *gs = services_get_goal_store(g_services);
    action_store_t *as = services_get_action_store(g_services);
    if (!gs || !as) {
        tool_result_set_error(result, "Stores not available");
        free(goal_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, goal_id);
    if (!goal) {
        tool_result_set_error(result, "Goal not found");
        free(goal_id);
        return 0;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON_AddStringToObject(json, "id", goal->id);
    cJSON_AddStringToObject(json, "name", goal->name);
    if (goal->description)
        cJSON_AddStringToObject(json, "description", goal->description);
    cJSON_AddStringToObject(json, "status", goal_status_to_string(goal->status));

    if (goal->goal_state) {
        cJSON *gst = cJSON_Parse(goal->goal_state);
        if (gst) cJSON_AddItemToObject(json, "goal_state", gst);
    }
    if (goal->world_state) {
        cJSON *wst = cJSON_Parse(goal->world_state);
        if (wst) cJSON_AddItemToObject(json, "world_state", wst);
    }

    GoapProgress gp = goap_check_progress(goal->goal_state, goal->world_state);
    cJSON_AddNumberToObject(json, "assertions_satisfied", gp.satisfied);
    cJSON_AddNumberToObject(json, "assertions_total", gp.total);

    if (goal->summary)
        cJSON_AddStringToObject(json, "summary", goal->summary);
    cJSON_AddNumberToObject(json, "supervisor_pid", (double)goal->supervisor_pid);

    /* Action counts by status */
    cJSON *counts = cJSON_CreateObject();
    cJSON_AddNumberToObject(counts, "pending",
        (double)action_store_count_by_status(as, goal_id, ACTION_STATUS_PENDING));
    cJSON_AddNumberToObject(counts, "running",
        (double)action_store_count_by_status(as, goal_id, ACTION_STATUS_RUNNING));
    cJSON_AddNumberToObject(counts, "completed",
        (double)action_store_count_by_status(as, goal_id, ACTION_STATUS_COMPLETED));
    cJSON_AddNumberToObject(counts, "failed",
        (double)action_store_count_by_status(as, goal_id, ACTION_STATUS_FAILED));
    cJSON_AddNumberToObject(counts, "skipped",
        (double)action_store_count_by_status(as, goal_id, ACTION_STATUS_SKIPPED));
    cJSON_AddItemToObject(json, "action_counts", counts);

    /* Action tree */
    size_t action_count = 0;
    Action **actions = action_store_list_by_goal(as, goal_id, &action_count);
    cJSON *tree = cJSON_CreateArray();
    build_action_tree(tree, actions, action_count, NULL);
    cJSON_AddItemToObject(json, "action_tree", tree);
    action_free_list(actions, action_count);

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;

    cJSON_Delete(json);
    goal_free(goal);
    free(goal_id);
    return 0;
}

/* ========================================================================
 * start_goal
 * ======================================================================== */

int execute_start_goal(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        tool_result_set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    goal_store_t *gs = services_get_goal_store(g_services);
    if (!gs) {
        tool_result_set_error(result, "Goal store not available");
        free(goal_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, goal_id);
    if (!goal) {
        tool_result_set_error(result, "Goal not found");
        free(goal_id);
        return 0;
    }

    if (goal->status != GOAL_STATUS_PLANNING && goal->status != GOAL_STATUS_PAUSED) {
        char err[128];
        snprintf(err, sizeof(err), "Cannot start goal in %s status (must be planning or paused)",
                 goal_status_to_string(goal->status));
        tool_result_set_error(result, err);
        goal_free(goal);
        free(goal_id);
        return 0;
    }

    if (goal->supervisor_pid > 0 && orchestrator_supervisor_alive(gs, goal_id)) {
        tool_result_set_error(result, "Supervisor already running for this goal");
        goal_free(goal);
        free(goal_id);
        return 0;
    }

    GoalStatus original_status = goal->status;

    /* PAUSED goals resume as ACTIVE (they were previously planned).
     * PLANNING goals stay PLANNING — the planner phase will transition
     * them to ACTIVE after plan_is_complete() fires. */
    if (original_status == GOAL_STATUS_PAUSED) {
        int rc = goal_store_update_status(gs, goal_id, GOAL_STATUS_ACTIVE);
        if (rc != 0) {
            tool_result_set_error(result, "Failed to activate goal");
            goal_free(goal);
            free(goal_id);
            return 0;
        }
    }

    int rc = orchestrator_spawn_supervisor(gs, goal_id);
    if (rc != 0) {
        tool_result_set_error(result, "Failed to spawn supervisor");
        if (original_status == GOAL_STATUS_PAUSED)
            goal_store_update_status(gs, goal_id, original_status);
        goal_free(goal);
        free(goal_id);
        return 0;
    }

    /* Re-read to get the updated PID and current status */
    goal_free(goal);
    goal = goal_store_get(gs, goal_id);

    const char *status_str = goal ? goal_status_to_string(goal->status) : "unknown";

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON_AddStringToObject(json, "goal_id", goal_id);
    cJSON_AddStringToObject(json, "status", status_str);
    if (goal)
        cJSON_AddNumberToObject(json, "supervisor_pid", (double)goal->supervisor_pid);

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;

    cJSON_Delete(json);
    goal_free(goal);
    free(goal_id);
    return 0;
}

/* ========================================================================
 * pause_goal
 * ======================================================================== */

int execute_pause_goal(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        tool_result_set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    goal_store_t *gs = services_get_goal_store(g_services);
    if (!gs) {
        tool_result_set_error(result, "Goal store not available");
        free(goal_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, goal_id);
    if (!goal) {
        tool_result_set_error(result, "Goal not found");
        free(goal_id);
        return 0;
    }

    if (goal->status != GOAL_STATUS_ACTIVE) {
        char err[128];
        snprintf(err, sizeof(err), "Cannot pause goal in %s status (must be active)",
                 goal_status_to_string(goal->status));
        tool_result_set_error(result, err);
        goal_free(goal);
        free(goal_id);
        return 0;
    }

    /* Kill supervisor — orchestrator_kill_supervisor updates status to PAUSED */
    int rc = orchestrator_kill_supervisor(gs, goal_id);
    goal_free(goal);

    if (rc == 0) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "success", cJSON_True);
        cJSON_AddStringToObject(json, "goal_id", goal_id);
        cJSON_AddStringToObject(json, "status", "paused");
        result->result = cJSON_PrintUnformatted(json);
        result->success = 1;
        cJSON_Delete(json);
    } else {
        tool_result_set_error(result, "Failed to pause goal (no supervisor running?)");
    }

    free(goal_id);
    return 0;
}

/* ========================================================================
 * cancel_goal
 * ======================================================================== */

int execute_cancel_goal(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        tool_result_set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    goal_store_t *gs = services_get_goal_store(g_services);
    if (!gs) {
        tool_result_set_error(result, "Goal store not available");
        free(goal_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, goal_id);
    if (!goal) {
        tool_result_set_error(result, "Goal not found");
        free(goal_id);
        return 0;
    }

    if (goal->status == GOAL_STATUS_COMPLETED || goal->status == GOAL_STATUS_FAILED) {
        char err[128];
        snprintf(err, sizeof(err), "Goal already in terminal state: %s",
                 goal_status_to_string(goal->status));
        tool_result_set_error(result, err);
        goal_free(goal);
        free(goal_id);
        return 0;
    }

    /* Kill supervisor if running */
    if (goal->supervisor_pid > 0) {
        orchestrator_kill_supervisor(gs, goal_id);
    }

    goal_store_update_status(gs, goal_id, GOAL_STATUS_FAILED);
    goal_free(goal);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON_AddStringToObject(json, "goal_id", goal_id);
    cJSON_AddStringToObject(json, "status", "failed");
    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;

    cJSON_Delete(json);
    free(goal_id);
    return 0;
}
