#include "action_store.h"
#include "sqlite_dal.h"
#include "util/uuid_utils.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct action_store {
    sqlite_dal_t *dal;
};

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS actions ("
    "    id TEXT PRIMARY KEY,"
    "    goal_id TEXT NOT NULL,"
    "    parent_action_id TEXT,"
    "    description TEXT NOT NULL,"
    "    preconditions TEXT DEFAULT '[]',"
    "    effects TEXT DEFAULT '[]',"
    "    is_compound INTEGER DEFAULT 0,"
    "    status INTEGER DEFAULT 0,"
    "    role TEXT DEFAULT 'implementation',"
    "    result TEXT,"
    "    attempt_count INTEGER DEFAULT 0,"
    "    created_at INTEGER NOT NULL,"
    "    updated_at INTEGER NOT NULL,"
    "    FOREIGN KEY (parent_action_id) REFERENCES actions(id) ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_actions_goal ON actions(goal_id);"
    "CREATE INDEX IF NOT EXISTS idx_actions_goal_status ON actions(goal_id, status);"
    "CREATE INDEX IF NOT EXISTS idx_actions_parent ON actions(parent_action_id);";

#define ACTION_COLUMNS \
    "id, goal_id, parent_action_id, description, preconditions, effects, " \
    "is_compound, status, role, result, attempt_count, created_at, updated_at"

static void *map_action(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    Action *action = calloc(1, sizeof(Action));
    if (action == NULL) return NULL;

    const char *id = (const char *)sqlite3_column_text(stmt, 0);
    const char *goal_id = (const char *)sqlite3_column_text(stmt, 1);
    const char *parent_id = (const char *)sqlite3_column_text(stmt, 2);
    const char *description = (const char *)sqlite3_column_text(stmt, 3);
    const char *preconditions = (const char *)sqlite3_column_text(stmt, 4);
    const char *effects = (const char *)sqlite3_column_text(stmt, 5);
    const char *role = (const char *)sqlite3_column_text(stmt, 8);
    const char *result = (const char *)sqlite3_column_text(stmt, 9);

    if (id) strncpy(action->id, id, sizeof(action->id) - 1);
    if (goal_id) strncpy(action->goal_id, goal_id, sizeof(action->goal_id) - 1);
    if (parent_id) strncpy(action->parent_action_id, parent_id, sizeof(action->parent_action_id) - 1);
    if (role) strncpy(action->role, role, sizeof(action->role) - 1);

    if (description) {
        action->description = strdup(description);
        if (!action->description) { free(action); return NULL; }
    }
    if (preconditions) {
        action->preconditions = strdup(preconditions);
        if (!action->preconditions) { free(action->description); free(action); return NULL; }
    }
    if (effects) {
        action->effects = strdup(effects);
        if (!action->effects) { free(action->preconditions); free(action->description); free(action); return NULL; }
    }
    if (result) {
        action->result = strdup(result);
        if (!action->result) { free(action->effects); free(action->preconditions); free(action->description); free(action); return NULL; }
    }

    action->is_compound = sqlite3_column_int(stmt, 6) != 0;
    action->status = (ActionStatus)sqlite3_column_int(stmt, 7);
    action->attempt_count = sqlite3_column_int(stmt, 10);
    action->created_at = (time_t)sqlite3_column_int64(stmt, 11);
    action->updated_at = (time_t)sqlite3_column_int64(stmt, 12);
    return action;
}

action_store_t *action_store_create(const char *db_path) {
    action_store_t *store = calloc(1, sizeof(action_store_t));
    if (store == NULL) return NULL;

    sqlite_dal_config_t config = SQLITE_DAL_CONFIG_DEFAULT;
    config.db_path = db_path;
    config.default_name = "scaffold.db";
    config.schema_sql = SCHEMA_SQL;

    store->dal = sqlite_dal_create(&config);
    if (store->dal == NULL) {
        free(store);
        return NULL;
    }
    return store;
}

void action_store_destroy(action_store_t *store) {
    if (store == NULL) return;
    sqlite_dal_destroy(store->dal);
    free(store);
}

int action_store_insert(action_store_t *store, const char *goal_id,
                        const char *parent_action_id, const char *description,
                        const char *preconditions_json, const char *effects_json,
                        bool is_compound, const char *role, char *out_id) {
    if (!store || !goal_id || !description || !out_id) return -1;

    char action_id[40] = {0};
    if (uuid_generate_v4(action_id) != 0) return -1;

    time_t now = time(NULL);
    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO actions (id, goal_id, parent_action_id, description, "
        "preconditions, effects, is_compound, status, role, attempt_count, "
        "created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, 0, ?, ?);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, action_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, goal_id, -1, SQLITE_STATIC);
    if (parent_action_id && parent_action_id[0])
        sqlite3_bind_text(stmt, 3, parent_action_id, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(stmt, 3);
    sqlite3_bind_text(stmt, 4, description, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, preconditions_json ? preconditions_json : "[]", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, effects_json ? effects_json : "[]", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, is_compound ? 1 : 0);
    sqlite3_bind_text(stmt, 8, role ? role : "implementation", -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 9, now);
    sqlite3_bind_int64(stmt, 10, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    if (rc != SQLITE_DONE) return -1;
    strcpy(out_id, action_id);
    return 0;
}

Action *action_store_get(action_store_t *store, const char *id) {
    if (!store || !id) return NULL;
    BindText1 params = { id };
    return sqlite_dal_query_one_p(store->dal,
        "SELECT " ACTION_COLUMNS " FROM actions WHERE id = ?;",
        bind_text1, &params, map_action, NULL);
}

int action_store_update_status(action_store_t *store, const char *id,
                               ActionStatus status, const char *result) {
    if (!store || !id) return -1;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "UPDATE actions SET status = ?, updated_at = ?, result = ? WHERE id = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    if (result) sqlite3_bind_text(stmt, 3, result, -1, SQLITE_STATIC);
    else sqlite3_bind_null(stmt, 3);
    sqlite3_bind_text(stmt, 4, id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

static bool preconditions_satisfied(const char *preconditions_json,
                                     const char *world_state_json) {
    if (!preconditions_json || strcmp(preconditions_json, "[]") == 0) return true;

    cJSON *preconditions = cJSON_Parse(preconditions_json);
    if (!preconditions) return true;
    if (!cJSON_IsArray(preconditions)) { cJSON_Delete(preconditions); return true; }
    if (cJSON_GetArraySize(preconditions) == 0) { cJSON_Delete(preconditions); return true; }

    cJSON *world_state = cJSON_Parse(world_state_json);
    if (!world_state) { cJSON_Delete(preconditions); return false; }

    bool satisfied = true;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, preconditions) {
        if (!cJSON_IsString(item)) continue;
        const char *key = item->valuestring;
        cJSON *val = cJSON_GetObjectItemCaseSensitive(world_state, key);
        if (!val || !cJSON_IsTrue(val)) {
            satisfied = false;
            break;
        }
    }

    cJSON_Delete(preconditions);
    cJSON_Delete(world_state);
    return satisfied;
}

Action **action_store_list_ready(action_store_t *store, const char *goal_id,
                                 const char *world_state_json, size_t *count) {
    if (!store || !goal_id || !count) return NULL;
    *count = 0;

    /* Fetch all PENDING actions for this goal */
    BindText1 params = { goal_id };
    void **items = NULL;
    size_t total = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT " ACTION_COLUMNS " FROM actions "
        "WHERE goal_id = ? AND status = 0 ORDER BY created_at;",
        bind_text1, &params, map_action, (sqlite_item_free_t)action_free,
        NULL, &items, &total);

    if (rc != 0 || total == 0) return NULL;

    /* Filter by precondition satisfaction */
    Action **ready = malloc(total * sizeof(Action *));
    if (!ready) {
        for (size_t i = 0; i < total; i++) action_free(items[i]);
        free(items);
        return NULL;
    }

    size_t ready_count = 0;
    const char *ws = world_state_json ? world_state_json : "{}";
    for (size_t i = 0; i < total; i++) {
        Action *action = (Action *)items[i];
        if (preconditions_satisfied(action->preconditions, ws)) {
            ready[ready_count++] = action;
        } else {
            action_free(action);
        }
    }
    free(items);

    if (ready_count == 0) { free(ready); return NULL; }
    *count = ready_count;
    return ready;
}

Action **action_store_list_by_goal(action_store_t *store, const char *goal_id,
                                    size_t *count) {
    if (!store || !goal_id || !count) return NULL;
    *count = 0;

    BindText1 params = { goal_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT " ACTION_COLUMNS " FROM actions WHERE goal_id = ? ORDER BY created_at;",
        bind_text1, &params, map_action, (sqlite_item_free_t)action_free,
        NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Action **)items;
}

Action **action_store_list_children(action_store_t *store,
                                     const char *parent_action_id, size_t *count) {
    if (!store || !parent_action_id || !count) return NULL;
    *count = 0;

    BindText1 params = { parent_action_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT " ACTION_COLUMNS " FROM actions WHERE parent_action_id = ? ORDER BY created_at;",
        bind_text1, &params, map_action, (sqlite_item_free_t)action_free,
        NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Action **)items;
}

int action_store_count_by_status(action_store_t *store, const char *goal_id,
                                  ActionStatus status) {
    if (!store || !goal_id) return -1;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM actions WHERE goal_id = ? AND status = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, goal_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, status);

    int result = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);
    return result;
}

int action_store_skip_pending(action_store_t *store, const char *goal_id) {
    if (!store || !goal_id) return -1;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "UPDATE actions SET status = ?, updated_at = ? WHERE goal_id = ? AND status = 0;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_int(stmt, 1, ACTION_STATUS_SKIPPED);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_text(stmt, 3, goal_id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    return (rc == SQLITE_DONE) ? changes : -1;
}

void action_free(Action *action) {
    if (!action) return;
    free(action->description);
    free(action->preconditions);
    free(action->effects);
    free(action->result);
    free(action);
}

void action_free_list(Action **actions, size_t count) {
    if (!actions) return;
    for (size_t i = 0; i < count; i++) action_free(actions[i]);
    free(actions);
}

const char *action_status_to_string(ActionStatus status) {
    switch (status) {
        case ACTION_STATUS_PENDING:   return "pending";
        case ACTION_STATUS_RUNNING:   return "running";
        case ACTION_STATUS_COMPLETED: return "completed";
        case ACTION_STATUS_FAILED:    return "failed";
        case ACTION_STATUS_SKIPPED:   return "skipped";
        default:                      return "pending";
    }
}

ActionStatus action_status_from_string(const char *status_str) {
    if (!status_str) return ACTION_STATUS_PENDING;
    if (strcmp(status_str, "running") == 0) return ACTION_STATUS_RUNNING;
    if (strcmp(status_str, "completed") == 0) return ACTION_STATUS_COMPLETED;
    if (strcmp(status_str, "failed") == 0) return ACTION_STATUS_FAILED;
    if (strcmp(status_str, "skipped") == 0) return ACTION_STATUS_SKIPPED;
    return ACTION_STATUS_PENDING;
}
