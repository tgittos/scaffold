#include "goap_tools.h"
#include "tool_param_dsl.h"
#include "../util/common_utils.h"
#include "../util/uuid_utils.h"
#include "../util/config.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "services/services.h"
#include "workflow/workflow.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESULT_PREVIEW 4000

static Services *g_services = NULL;

void goap_tools_set_services(Services *services) {
    g_services = services;
}

/* ========================================================================
 * Parameter definitions
 * ======================================================================== */

static const ParamDef GET_GOAL_PARAMS[] = {
    {"goal_id", "string", "ID of the goal", NULL, 1},
};

static const ParamDef LIST_ACTIONS_PARAMS[] = {
    {"goal_id", "string", "ID of the goal", NULL, 1},
    {"status", "string", "Filter by status: pending, running, completed, failed, skipped", NULL, 0},
    {"parent_action_id", "string", "Filter by parent action ID (list children of a compound action)", NULL, 0},
};

static const ParamDef CREATE_GOAL_PARAMS[] = {
    {"name", "string", "Short name for the goal", NULL, 1},
    {"description", "string", "Full goal description", NULL, 1},
    {"goal_state", "object", "Goal state: JSON object of boolean assertion keys that must all be true", NULL, 1},
    {"queue_name", "string", "Work queue name for this goal's workers (auto-generated if omitted)", NULL, 0},
};

static const ParamDef CREATE_ACTIONS_PARAMS[] = {
    {"goal_id", "string", "ID of the goal these actions belong to", NULL, 1},
    {"actions", "array", "Array of action objects, each with: description (string), preconditions (string array), effects (string array), is_compound (bool), role (string, optional)", NULL, 1},
};

static const ParamDef UPDATE_ACTION_PARAMS[] = {
    {"action_id", "string", "ID of the action to update", NULL, 1},
    {"status", "string", "New status: pending, running, completed, failed, skipped", NULL, 1},
    {"result", "string", "Result text (for completed/failed actions)", NULL, 0},
};

static const ParamDef DISPATCH_ACTION_PARAMS[] = {
    {"action_id", "string", "ID of the primitive action to dispatch to a worker", NULL, 1},
};

static const ParamDef UPDATE_WORLD_STATE_PARAMS[] = {
    {"goal_id", "string", "ID of the goal", NULL, 1},
    {"assertions", "object", "JSON object of assertion key/boolean pairs to merge into world state", NULL, 1},
};

static const ParamDef CHECK_COMPLETE_PARAMS[] = {
    {"goal_id", "string", "ID of the goal to check", NULL, 1},
};

static const ParamDef GET_ACTION_RESULTS_PARAMS[] = {
    {"goal_id", "string", "ID of the goal", NULL, 1},
    {"action_ids", "array", "Optional: specific action IDs to get results for (omit for all completed)", NULL, 0},
};

/* ========================================================================
 * Tool definitions table
 * ======================================================================== */

static const ToolDef GOAP_TOOLS[] = {
    {"goap_get_goal",
     "Get goal details: description, goal state, world state, status, summary",
     GET_GOAL_PARAMS, 1, execute_goap_get_goal},
    {"goap_list_actions",
     "List actions for a goal, optionally filtered by status or parent compound action",
     LIST_ACTIONS_PARAMS, 3, execute_goap_list_actions},
    {"goap_create_goal",
     "Create a new goal with goal state assertions defining completion criteria",
     CREATE_GOAL_PARAMS, 4, execute_goap_create_goal},
    {"goap_create_actions",
     "Batch-create actions (compound or primitive) with preconditions and effects",
     CREATE_ACTIONS_PARAMS, 2, execute_goap_create_actions},
    {"goap_update_action",
     "Update an action's status and optionally set its result",
     UPDATE_ACTION_PARAMS, 3, execute_goap_update_action},
    {"goap_dispatch_action",
     "Dispatch a primitive action to a worker: enqueue work item and spawn worker process",
     DISPATCH_ACTION_PARAMS, 1, execute_goap_dispatch_action},
    {"goap_update_world_state",
     "Merge boolean assertions into a goal's world state after verifying effects",
     UPDATE_WORLD_STATE_PARAMS, 2, execute_goap_update_world_state},
    {"goap_check_complete",
     "Check if a goal is complete: world_state contains all goal_state assertions as true",
     CHECK_COMPLETE_PARAMS, 1, execute_goap_check_complete},
    {"goap_get_action_results",
     "Get results from completed actions for a goal (results truncated to prevent context blowup)",
     GET_ACTION_RESULTS_PARAMS, 2, execute_goap_get_action_results},
};

#define GOAP_TOOL_COUNT (sizeof(GOAP_TOOLS) / sizeof(GOAP_TOOLS[0]))

int register_goap_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    g_services = registry->services;
    int registered = register_tools_from_defs(registry, GOAP_TOOLS, GOAP_TOOL_COUNT);
    return (registered == (int)GOAP_TOOL_COUNT) ? 0 : -1;
}

/* ========================================================================
 * Helpers
 * ======================================================================== */

static bool is_valid_action_status(const char *s) {
    return s && (strcmp(s, "pending") == 0 || strcmp(s, "running") == 0 ||
                 strcmp(s, "completed") == 0 || strcmp(s, "failed") == 0 ||
                 strcmp(s, "skipped") == 0);
}

static void set_error(ToolResult *result, const char *msg) {
    char buf[1200];
    snprintf(buf, sizeof(buf), "{\"success\":false,\"error\":\"%s\"}", msg);
    result->result = safe_strdup(buf);
    result->success = 0;
}

static cJSON *action_to_json(const Action *a) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", a->id);
    cJSON_AddStringToObject(obj, "goal_id", a->goal_id);
    if (a->parent_action_id[0])
        cJSON_AddStringToObject(obj, "parent_action_id", a->parent_action_id);
    if (a->description)
        cJSON_AddStringToObject(obj, "description", a->description);
    cJSON_AddStringToObject(obj, "status", action_status_to_string(a->status));
    cJSON_AddBoolToObject(obj, "is_compound", a->is_compound);
    if (a->role[0])
        cJSON_AddStringToObject(obj, "role", a->role);
    if (a->preconditions) {
        cJSON *p = cJSON_Parse(a->preconditions);
        if (p) cJSON_AddItemToObject(obj, "preconditions", p);
    }
    if (a->effects) {
        cJSON *e = cJSON_Parse(a->effects);
        if (e) cJSON_AddItemToObject(obj, "effects", e);
    }
    if (a->result)
        cJSON_AddStringToObject(obj, "result", a->result);
    cJSON_AddNumberToObject(obj, "attempt_count", a->attempt_count);
    return obj;
}

/* ========================================================================
 * goap_get_goal
 * ======================================================================== */

int execute_goap_get_goal(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    goal_store_t *gs = services_get_goal_store(g_services);
    if (!gs) {
        set_error(result, "Goal store not available");
        free(goal_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, goal_id);
    free(goal_id);
    if (!goal) {
        set_error(result, "Goal not found");
        return 0;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON_AddStringToObject(json, "id", goal->id);
    cJSON_AddStringToObject(json, "name", goal->name);
    if (goal->description)
        cJSON_AddStringToObject(json, "description", goal->description);
    cJSON_AddStringToObject(json, "status", goal_status_to_string(goal->status));
    if (goal->queue_name[0])
        cJSON_AddStringToObject(json, "queue_name", goal->queue_name);
    if (goal->summary)
        cJSON_AddStringToObject(json, "summary", goal->summary);

    if (goal->goal_state) {
        cJSON *gs_obj = cJSON_Parse(goal->goal_state);
        if (gs_obj) cJSON_AddItemToObject(json, "goal_state", gs_obj);
        else cJSON_AddStringToObject(json, "goal_state_raw", goal->goal_state);
    }
    if (goal->world_state) {
        cJSON *ws_obj = cJSON_Parse(goal->world_state);
        if (ws_obj) cJSON_AddItemToObject(json, "world_state", ws_obj);
        else cJSON_AddStringToObject(json, "world_state_raw", goal->world_state);
    }

    cJSON_AddNumberToObject(json, "supervisor_pid", (double)goal->supervisor_pid);

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;
    cJSON_Delete(json);
    goal_free(goal);
    return 0;
}

/* ========================================================================
 * goap_list_actions
 * ======================================================================== */

int execute_goap_list_actions(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    char *status_str = extract_string_param(tc->arguments, "status");
    char *parent_id = extract_string_param(tc->arguments, "parent_action_id");

    if (status_str && !is_valid_action_status(status_str)) {
        set_error(result, "Invalid status filter (must be: pending, running, completed, failed, skipped)");
        free(goal_id);
        free(status_str);
        free(parent_id);
        return 0;
    }

    action_store_t *as = services_get_action_store(g_services);
    if (!as) {
        set_error(result, "Action store not available");
        free(goal_id);
        free(status_str);
        free(parent_id);
        return 0;
    }

    Action **actions = NULL;
    size_t count = 0;

    if (parent_id) {
        actions = action_store_list_children(as, parent_id, &count);
    } else {
        actions = action_store_list_by_goal(as, goal_id, &count);
    }

    bool has_status_filter = (status_str != NULL);
    ActionStatus filter_status = has_status_filter ? action_status_from_string(status_str) : 0;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON *arr = cJSON_CreateArray();

    for (size_t i = 0; i < count; i++) {
        if (has_status_filter && actions[i]->status != filter_status)
            continue;
        cJSON_AddItemToArray(arr, action_to_json(actions[i]));
    }

    cJSON_AddItemToObject(json, "actions", arr);
    cJSON_AddNumberToObject(json, "count", cJSON_GetArraySize(arr));

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;

    cJSON_Delete(json);
    action_free_list(actions, count);
    free(goal_id);
    free(status_str);
    free(parent_id);
    return 0;
}

/* ========================================================================
 * goap_create_goal
 * ======================================================================== */

int execute_goap_create_goal(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    cJSON *args = cJSON_Parse(tc->arguments);
    if (!args) {
        set_error(result, "Invalid JSON arguments");
        return 0;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
    const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(args, "description"));
    const char *queue_name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "queue_name"));

    if (!name || !description) {
        set_error(result, "Missing required parameters: name, description");
        cJSON_Delete(args);
        return 0;
    }

    /* goal_state can be a JSON object (LLM sends it directly) or a string */
    char *goal_state_str = NULL;
    cJSON *gs_item = cJSON_GetObjectItem(args, "goal_state");
    if (gs_item) {
        if (cJSON_IsObject(gs_item))
            goal_state_str = cJSON_PrintUnformatted(gs_item);
        else if (cJSON_IsString(gs_item))
            goal_state_str = safe_strdup(cJSON_GetStringValue(gs_item));
    }

    /* Auto-generate queue name if not provided */
    char queue_buf[64] = {0};
    if (!queue_name) {
        char uuid_buf[40];
        uuid_generate_v4(uuid_buf);
        snprintf(queue_buf, sizeof(queue_buf), "goal_%.32s", uuid_buf);
        queue_name = queue_buf;
    }

    goal_store_t *store = services_get_goal_store(g_services);
    if (!store) {
        set_error(result, "Goal store not available");
        free(goal_state_str);
        cJSON_Delete(args);
        return 0;
    }

    char goal_id[40] = {0};
    int rc = goal_store_insert(store, name, description, goal_state_str, queue_name, goal_id);

    if (rc == 0) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", cJSON_True);
        cJSON_AddStringToObject(resp, "goal_id", goal_id);
        cJSON_AddStringToObject(resp, "queue_name", queue_name);
        result->result = cJSON_PrintUnformatted(resp);
        result->success = 1;
        cJSON_Delete(resp);
    } else {
        set_error(result, "Failed to create goal");
    }

    free(goal_state_str);
    cJSON_Delete(args);
    return 0;
}

/* ========================================================================
 * goap_create_actions
 * ======================================================================== */

int execute_goap_create_actions(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    cJSON *args = cJSON_Parse(tc->arguments);
    if (!args) {
        set_error(result, "Invalid JSON arguments");
        return 0;
    }

    const char *goal_id = cJSON_GetStringValue(cJSON_GetObjectItem(args, "goal_id"));
    cJSON *actions_arr = cJSON_GetObjectItem(args, "actions");

    if (!goal_id || !actions_arr || !cJSON_IsArray(actions_arr)) {
        set_error(result, "Missing required parameters: goal_id, actions (array)");
        cJSON_Delete(args);
        return 0;
    }

    action_store_t *as = services_get_action_store(g_services);
    if (!as) {
        set_error(result, "Action store not available");
        cJSON_Delete(args);
        return 0;
    }

    cJSON *ids = cJSON_CreateArray();
    int failed = 0;

    cJSON *action_obj;
    cJSON_ArrayForEach(action_obj, actions_arr) {
        const char *desc = cJSON_GetStringValue(
            cJSON_GetObjectItem(action_obj, "description"));
        const char *parent = cJSON_GetStringValue(
            cJSON_GetObjectItem(action_obj, "parent_action_id"));
        const char *role = cJSON_GetStringValue(
            cJSON_GetObjectItem(action_obj, "role"));

        cJSON *precond = cJSON_GetObjectItem(action_obj, "preconditions");
        char *precond_str = (precond && cJSON_IsArray(precond))
            ? cJSON_PrintUnformatted(precond) : safe_strdup("[]");

        cJSON *effects = cJSON_GetObjectItem(action_obj, "effects");
        char *effects_str = (effects && cJSON_IsArray(effects))
            ? cJSON_PrintUnformatted(effects) : NULL;

        cJSON *compound = cJSON_GetObjectItem(action_obj, "is_compound");
        bool is_compound = cJSON_IsTrue(compound);

        if (!desc || !effects_str) {
            failed++;
            free(precond_str);
            free(effects_str);
            continue;
        }

        char out_id[40] = {0};
        int rc = action_store_insert(as, goal_id, parent, desc,
                                     precond_str, effects_str,
                                     is_compound, role, out_id);
        if (rc == 0) {
            cJSON_AddItemToArray(ids, cJSON_CreateString(out_id));
        } else {
            failed++;
        }

        free(precond_str);
        free(effects_str);
    }

    int created = cJSON_GetArraySize(ids);
    bool ok = (created > 0 || failed == 0);

    cJSON *json = cJSON_CreateObject();
    if (ok)
        cJSON_AddBoolToObject(json, "success", cJSON_True);
    else
        cJSON_AddFalseToObject(json, "success");
    cJSON_AddItemToObject(json, "action_ids", ids);
    cJSON_AddNumberToObject(json, "created", created);
    if (failed > 0)
        cJSON_AddNumberToObject(json, "failed", failed);

    result->result = cJSON_PrintUnformatted(json);
    result->success = ok ? 1 : 0;

    cJSON_Delete(json);
    cJSON_Delete(args);
    return 0;
}

/* ========================================================================
 * goap_update_action
 * ======================================================================== */

int execute_goap_update_action(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *action_id = extract_string_param(tc->arguments, "action_id");
    char *status_str = extract_string_param(tc->arguments, "status");
    char *result_text = extract_string_param(tc->arguments, "result");

    if (!action_id || !status_str) {
        set_error(result, "Missing required parameters: action_id, status");
        free(action_id);
        free(status_str);
        free(result_text);
        return 0;
    }

    if (!is_valid_action_status(status_str)) {
        set_error(result, "Invalid status (must be: pending, running, completed, failed, skipped)");
        free(action_id);
        free(status_str);
        free(result_text);
        return 0;
    }

    action_store_t *as = services_get_action_store(g_services);
    if (!as) {
        set_error(result, "Action store not available");
        free(action_id);
        free(status_str);
        free(result_text);
        return 0;
    }

    ActionStatus status = action_status_from_string(status_str);
    int rc = action_store_update_status(as, action_id, status, result_text);

    if (rc == 0) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", cJSON_True);
        cJSON_AddStringToObject(resp, "action_id", action_id);
        cJSON_AddStringToObject(resp, "status", action_status_to_string(status));
        result->result = cJSON_PrintUnformatted(resp);
        result->success = 1;
        cJSON_Delete(resp);
    } else {
        set_error(result, "Failed to update action");
    }

    free(action_id);
    free(status_str);
    free(result_text);
    return 0;
}

/* ========================================================================
 * goap_dispatch_action (worker spawning)
 * ======================================================================== */

static char *build_work_context(const Goal *goal, action_store_t *as,
                                Action *action) {

    cJSON *ctx = cJSON_CreateObject();
    cJSON_AddStringToObject(ctx, "goal",
        goal->description ? goal->description : goal->name);
    cJSON_AddStringToObject(ctx, "action", action->description);
    cJSON_AddStringToObject(ctx, "role",
        action->role[0] ? action->role : "implementation");

    if (goal->world_state) {
        cJSON *ws = cJSON_Parse(goal->world_state);
        if (ws) cJSON_AddItemToObject(ctx, "world_state", ws);
    }

    /* Collect results from completed actions whose effects overlap with
       this action's preconditions — the prerequisite chain. */
    if (action->preconditions) {
        cJSON *prereqs = cJSON_Parse(action->preconditions);
        if (prereqs && cJSON_IsArray(prereqs) && cJSON_GetArraySize(prereqs) > 0) {
            cJSON *prereq_results = cJSON_CreateObject();
            size_t all_count = 0;
            Action **all_actions = action_store_list_by_goal(as, action->goal_id, &all_count);

            for (size_t i = 0; i < all_count; i++) {
                if (all_actions[i]->status != ACTION_STATUS_COMPLETED) continue;
                if (!all_actions[i]->result || !all_actions[i]->effects) continue;

                cJSON *effects = cJSON_Parse(all_actions[i]->effects);
                if (!effects || !cJSON_IsArray(effects)) {
                    cJSON_Delete(effects);
                    continue;
                }

                bool overlap = false;
                cJSON *eff;
                cJSON_ArrayForEach(eff, effects) {
                    if (!cJSON_IsString(eff)) continue;
                    cJSON *pre;
                    cJSON_ArrayForEach(pre, prereqs) {
                        if (cJSON_IsString(pre) &&
                            strcmp(pre->valuestring, eff->valuestring) == 0) {
                            overlap = true;
                            break;
                        }
                    }
                    if (overlap) break;
                }
                cJSON_Delete(effects);

                if (overlap) {
                    size_t rlen = strlen(all_actions[i]->result);
                    if (rlen > MAX_RESULT_PREVIEW) {
                        char *trunc = malloc(MAX_RESULT_PREVIEW + 20);
                        if (trunc) {
                            snprintf(trunc, MAX_RESULT_PREVIEW + 20,
                                     "%.*s...[truncated]",
                                     (int)MAX_RESULT_PREVIEW,
                                     all_actions[i]->result);
                            cJSON_AddStringToObject(prereq_results,
                                all_actions[i]->id, trunc);
                            free(trunc);
                        }
                    } else {
                        cJSON_AddStringToObject(prereq_results,
                            all_actions[i]->id, all_actions[i]->result);
                    }
                }
            }

            if (cJSON_GetArraySize(prereq_results) > 0) {
                cJSON_AddItemToObject(ctx, "prerequisite_results", prereq_results);
            } else {
                cJSON_Delete(prereq_results);
            }
            action_free_list(all_actions, all_count);
        }
        cJSON_Delete(prereqs);
    }

    char *context_str = cJSON_PrintUnformatted(ctx);
    cJSON_Delete(ctx);
    return context_str;
}

int execute_goap_dispatch_action(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *action_id = extract_string_param(tc->arguments, "action_id");
    if (!action_id) {
        set_error(result, "Missing required parameter: action_id");
        return 0;
    }

    action_store_t *as = services_get_action_store(g_services);
    goal_store_t *gs = services_get_goal_store(g_services);
    if (!as || !gs) {
        set_error(result, "Stores not available");
        free(action_id);
        return 0;
    }

    Action *action = action_store_get(as, action_id);
    if (!action) {
        set_error(result, "Action not found");
        free(action_id);
        return 0;
    }

    if (action->is_compound) {
        set_error(result, "Cannot dispatch compound action - decompose it first");
        action_free(action);
        free(action_id);
        return 0;
    }

    if (action->status != ACTION_STATUS_PENDING) {
        set_error(result, "Action is not pending");
        action_free(action);
        free(action_id);
        return 0;
    }

    /* Check worker capacity */
    int running = action_store_count_by_status(as, action->goal_id,
                                                ACTION_STATUS_RUNNING);
    int max_workers = config_get_int("max_workers_per_goal", 3);
    if (running >= max_workers) {
        set_error(result, "Worker capacity reached for this goal");
        action_free(action);
        free(action_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, action->goal_id);
    if (!goal) {
        set_error(result, "Goal not found for action");
        action_free(action);
        free(action_id);
        return 0;
    }

    char *context = build_work_context(goal, as, action);

    WorkQueue *wq = work_queue_create(goal->queue_name);
    if (!wq) {
        set_error(result, "Failed to create work queue");
        goal_free(goal);
        action_free(action);
        free(action_id);
        free(context);
        return 0;
    }

    char work_item_id[40] = {0};
    int rc = work_queue_enqueue(wq, action->description, context, 3, work_item_id);
    if (rc != 0) {
        set_error(result, "Failed to enqueue work item");
        work_queue_destroy(wq);
        goal_free(goal);
        action_free(action);
        free(action_id);
        free(context);
        return 0;
    }

    WorkerHandle *worker = worker_spawn(goal->queue_name, NULL);
    if (!worker) {
        set_error(result, "Failed to spawn worker");
        work_queue_destroy(wq);
        goal_free(goal);
        action_free(action);
        free(action_id);
        free(context);
        return 0;
    }

    action_store_update_status(as, action_id, ACTION_STATUS_RUNNING, NULL);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", cJSON_True);
    cJSON_AddStringToObject(resp, "action_id", action_id);
    cJSON_AddNumberToObject(resp, "worker_pid", (double)worker->pid);
    cJSON_AddStringToObject(resp, "work_item_id", work_item_id);
    result->result = cJSON_PrintUnformatted(resp);
    result->success = 1;
    cJSON_Delete(resp);

    worker_handle_free(worker);
    work_queue_destroy(wq);
    goal_free(goal);
    action_free(action);
    free(action_id);
    free(context);
    return 0;
}

/* ========================================================================
 * goap_update_world_state
 * ======================================================================== */

int execute_goap_update_world_state(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    cJSON *args = cJSON_Parse(tc->arguments);
    if (!args) {
        set_error(result, "Invalid JSON arguments");
        free(goal_id);
        return 0;
    }

    cJSON *assertions = cJSON_GetObjectItem(args, "assertions");
    if (!assertions || !cJSON_IsObject(assertions)) {
        set_error(result, "Missing required parameter: assertions (object)");
        cJSON_Delete(args);
        free(goal_id);
        return 0;
    }

    goal_store_t *gs = services_get_goal_store(g_services);
    if (!gs) {
        set_error(result, "Goal store not available");
        cJSON_Delete(args);
        free(goal_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, goal_id);
    if (!goal) {
        set_error(result, "Goal not found");
        cJSON_Delete(args);
        free(goal_id);
        return 0;
    }

    /* Parse current world state and merge assertions */
    cJSON *world_state = goal->world_state
        ? cJSON_Parse(goal->world_state) : cJSON_CreateObject();
    if (!world_state) world_state = cJSON_CreateObject();

    cJSON *item;
    cJSON_ArrayForEach(item, assertions) {
        if (cJSON_IsBool(item)) {
            cJSON_DeleteItemFromObject(world_state, item->string);
            cJSON_AddBoolToObject(world_state, item->string, cJSON_IsTrue(item));
        }
    }

    char *new_ws = cJSON_PrintUnformatted(world_state);
    int rc = goal_store_update_world_state(gs, goal_id, new_ws);

    if (rc == 0) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", cJSON_True);
        /* cJSON_AddItemToObject transfers ownership of world_state */
        cJSON_AddItemToObject(resp, "world_state", world_state);
        result->result = cJSON_PrintUnformatted(resp);
        result->success = 1;
        cJSON_Delete(resp); /* also frees world_state */
    } else {
        set_error(result, "Failed to update world state");
        cJSON_Delete(world_state);
    }

    free(new_ws);
    goal_free(goal);
    cJSON_Delete(args);
    free(goal_id);
    return 0;
}

/* ========================================================================
 * goap_check_complete
 * ======================================================================== */

int execute_goap_check_complete(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    goal_store_t *gs = services_get_goal_store(g_services);
    if (!gs) {
        set_error(result, "Goal store not available");
        free(goal_id);
        return 0;
    }

    Goal *goal = goal_store_get(gs, goal_id);
    free(goal_id);
    if (!goal) {
        set_error(result, "Goal not found");
        return 0;
    }

    cJSON *goal_state = goal->goal_state ? cJSON_Parse(goal->goal_state) : NULL;
    cJSON *world_state = goal->world_state ? cJSON_Parse(goal->world_state) : NULL;

    bool complete = true;
    int satisfied = 0;
    int total = 0;
    cJSON *missing = cJSON_CreateArray();

    if (goal_state && cJSON_IsObject(goal_state)) {
        cJSON *gs_item;
        cJSON_ArrayForEach(gs_item, goal_state) {
            total++;
            cJSON *ws_val = world_state
                ? cJSON_GetObjectItem(world_state, gs_item->string) : NULL;
            if (ws_val && cJSON_IsTrue(ws_val)) {
                satisfied++;
            } else {
                complete = false;
                cJSON_AddItemToArray(missing, cJSON_CreateString(gs_item->string));
            }
        }
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON_AddBoolToObject(json, "complete", complete);
    cJSON_AddNumberToObject(json, "satisfied", satisfied);
    cJSON_AddNumberToObject(json, "total", total);
    if (!complete)
        cJSON_AddItemToObject(json, "missing", missing);
    else
        cJSON_Delete(missing);

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;

    cJSON_Delete(json);
    cJSON_Delete(goal_state);
    cJSON_Delete(world_state);
    goal_free(goal);
    return 0;
}

/* ========================================================================
 * goap_get_action_results
 * ======================================================================== */

int execute_goap_get_action_results(const ToolCall *tc, ToolResult *result) {
    if (!tc || !result) return -1;
    result->tool_call_id = safe_strdup(tc->id);

    char *goal_id = extract_string_param(tc->arguments, "goal_id");
    if (!goal_id) {
        set_error(result, "Missing required parameter: goal_id");
        return 0;
    }

    action_store_t *as = services_get_action_store(g_services);
    if (!as) {
        set_error(result, "Action store not available");
        free(goal_id);
        return 0;
    }

    /* Optional action_ids filter */
    cJSON *args = cJSON_Parse(tc->arguments);
    cJSON *filter_ids = args ? cJSON_GetObjectItem(args, "action_ids") : NULL;

    size_t count = 0;
    Action **actions = action_store_list_by_goal(as, goal_id, &count);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON *results_arr = cJSON_CreateArray();

    for (size_t i = 0; i < count; i++) {
        if (actions[i]->status != ACTION_STATUS_COMPLETED) continue;
        if (!actions[i]->result) continue;

        /* Apply action_ids filter if provided */
        if (filter_ids && cJSON_IsArray(filter_ids)) {
            bool found = false;
            cJSON *fid;
            cJSON_ArrayForEach(fid, filter_ids) {
                if (cJSON_IsString(fid) &&
                    strcmp(fid->valuestring, actions[i]->id) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) continue;
        }

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "action_id", actions[i]->id);
        if (actions[i]->description)
            cJSON_AddStringToObject(item, "description", actions[i]->description);
        if (actions[i]->role[0])
            cJSON_AddStringToObject(item, "role", actions[i]->role);

        size_t rlen = strlen(actions[i]->result);
        if (rlen > MAX_RESULT_PREVIEW) {
            char *trunc = malloc(MAX_RESULT_PREVIEW + 20);
            if (trunc) {
                snprintf(trunc, MAX_RESULT_PREVIEW + 20,
                         "%.*s...[truncated]",
                         (int)MAX_RESULT_PREVIEW, actions[i]->result);
                cJSON_AddStringToObject(item, "result", trunc);
                cJSON_AddBoolToObject(item, "truncated", cJSON_True);
                free(trunc);
            }
        } else {
            cJSON_AddStringToObject(item, "result", actions[i]->result);
        }

        cJSON_AddItemToArray(results_arr, item);
    }

    cJSON_AddItemToObject(json, "results", results_arr);
    cJSON_AddNumberToObject(json, "count", cJSON_GetArraySize(results_arr));

    result->result = cJSON_PrintUnformatted(json);
    result->success = 1;

    cJSON_Delete(json);
    if (args) cJSON_Delete(args);
    action_free_list(actions, count);
    free(goal_id);
    return 0;
}
