#include "goal_store.h"
#include "sqlite_dal.h"
#include "util/uuid_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct goal_store {
    sqlite_dal_t *dal;
};

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS goals ("
    "    id TEXT PRIMARY KEY,"
    "    name TEXT NOT NULL,"
    "    description TEXT,"
    "    goal_state TEXT DEFAULT '{}',"
    "    world_state TEXT DEFAULT '{}',"
    "    summary TEXT,"
    "    status INTEGER DEFAULT 0,"
    "    queue_name TEXT NOT NULL,"
    "    supervisor_pid INTEGER DEFAULT 0,"
    "    supervisor_started_at INTEGER DEFAULT 0,"
    "    created_at INTEGER NOT NULL,"
    "    updated_at INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_goals_status ON goals(status);";

static void *map_goal(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    Goal *goal = calloc(1, sizeof(Goal));
    if (goal == NULL) return NULL;

    const char *id = (const char *)sqlite3_column_text(stmt, 0);
    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    const char *description = (const char *)sqlite3_column_text(stmt, 2);
    const char *goal_state = (const char *)sqlite3_column_text(stmt, 3);
    const char *world_state = (const char *)sqlite3_column_text(stmt, 4);
    const char *summary = (const char *)sqlite3_column_text(stmt, 5);
    const char *queue_name = (const char *)sqlite3_column_text(stmt, 8);

    if (id) strncpy(goal->id, id, sizeof(goal->id) - 1);
    if (name) strncpy(goal->name, name, sizeof(goal->name) - 1);
    if (queue_name) strncpy(goal->queue_name, queue_name, sizeof(goal->queue_name) - 1);

    if (description) {
        goal->description = strdup(description);
        if (!goal->description) { free(goal); return NULL; }
    }
    if (goal_state) {
        goal->goal_state = strdup(goal_state);
        if (!goal->goal_state) { free(goal->description); free(goal); return NULL; }
    }
    if (world_state) {
        goal->world_state = strdup(world_state);
        if (!goal->world_state) { free(goal->goal_state); free(goal->description); free(goal); return NULL; }
    }
    if (summary) {
        goal->summary = strdup(summary);
        if (!goal->summary) { free(goal->world_state); free(goal->goal_state); free(goal->description); free(goal); return NULL; }
    }

    goal->status = (GoalStatus)sqlite3_column_int(stmt, 6);
    goal->supervisor_pid = (pid_t)sqlite3_column_int(stmt, 7);
    goal->supervisor_started_at = sqlite3_column_int64(stmt, 9);
    goal->created_at = (time_t)sqlite3_column_int64(stmt, 10);
    goal->updated_at = (time_t)sqlite3_column_int64(stmt, 11);
    return goal;
}

/* Additional binder helpers */
typedef struct { int i1; int64_t i2; const char *s1; } BindIntInt64Text;
typedef struct { const char *s1; int64_t i2; const char *s2; } BindTextInt64Text;
typedef struct { int i1; int64_t i2; int64_t i3; const char *s1; } BindIntInt64Int64Text;

static int bind_int_int64_text(sqlite3_stmt *stmt, void *data) {
    BindIntInt64Text *b = data;
    sqlite3_bind_int(stmt, 1, b->i1);
    sqlite3_bind_int64(stmt, 2, b->i2);
    sqlite3_bind_text(stmt, 3, b->s1, -1, SQLITE_STATIC);
    return 0;
}

static int bind_text_int64_text(sqlite3_stmt *stmt, void *data) {
    BindTextInt64Text *b = data;
    sqlite3_bind_text(stmt, 1, b->s1, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, b->i2);
    sqlite3_bind_text(stmt, 3, b->s2, -1, SQLITE_STATIC);
    return 0;
}

static int bind_int_int64_int64_text(sqlite3_stmt *stmt, void *data) {
    BindIntInt64Int64Text *b = data;
    sqlite3_bind_int(stmt, 1, b->i1);
    sqlite3_bind_int64(stmt, 2, b->i2);
    sqlite3_bind_int64(stmt, 3, b->i3);
    sqlite3_bind_text(stmt, 4, b->s1, -1, SQLITE_STATIC);
    return 0;
}

goal_store_t *goal_store_create(const char *db_path) {
    goal_store_t *store = calloc(1, sizeof(goal_store_t));
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

void goal_store_destroy(goal_store_t *store) {
    if (store == NULL) return;
    sqlite_dal_destroy(store->dal);
    free(store);
}

#define GOAL_COLUMNS \
    "id, name, description, goal_state, world_state, summary, " \
    "status, supervisor_pid, queue_name, supervisor_started_at, created_at, updated_at"

int goal_store_insert(goal_store_t *store, const char *name,
                      const char *description, const char *goal_state_json,
                      const char *queue_name, char *out_id) {
    if (!store || !name || !queue_name || !out_id) return -1;

    char goal_id[40] = {0};
    if (uuid_generate_v4(goal_id) != 0) return -1;

    time_t now = time(NULL);
    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO goals (id, name, description, goal_state, world_state, "
        "status, queue_name, supervisor_pid, supervisor_started_at, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, '{}', 0, ?, 0, 0, ?, ?);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, goal_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    if (description) sqlite3_bind_text(stmt, 3, description, -1, SQLITE_STATIC);
    else sqlite3_bind_null(stmt, 3);
    if (goal_state_json) sqlite3_bind_text(stmt, 4, goal_state_json, -1, SQLITE_STATIC);
    else sqlite3_bind_text(stmt, 4, "{}", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, queue_name, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, now);
    sqlite3_bind_int64(stmt, 7, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    if (rc != SQLITE_DONE) return -1;
    strcpy(out_id, goal_id);
    return 0;
}

Goal *goal_store_get(goal_store_t *store, const char *id) {
    if (!store || !id) return NULL;
    BindText1 params = { id };
    return sqlite_dal_query_one_p(store->dal,
        "SELECT " GOAL_COLUMNS " FROM goals WHERE id = ?;",
        bind_text1, &params, map_goal, NULL);
}

int goal_store_update_status(goal_store_t *store, const char *id, GoalStatus status) {
    if (!store || !id) return -1;
    BindIntInt64Text params = { status, time(NULL), id };
    int changes = sqlite_dal_exec_p(store->dal,
        "UPDATE goals SET status = ?, updated_at = ? WHERE id = ?;",
        bind_int_int64_text, &params);
    return (changes > 0) ? 0 : -1;
}

int goal_store_update_world_state(goal_store_t *store, const char *id,
                                   const char *world_state_json) {
    if (!store || !id || !world_state_json) return -1;
    BindTextInt64Text params = { world_state_json, time(NULL), id };
    int changes = sqlite_dal_exec_p(store->dal,
        "UPDATE goals SET world_state = ?, updated_at = ? WHERE id = ?;",
        bind_text_int64_text, &params);
    return (changes > 0) ? 0 : -1;
}

int goal_store_update_summary(goal_store_t *store, const char *id, const char *summary) {
    if (!store || !id || !summary) return -1;
    BindTextInt64Text params = { summary, time(NULL), id };
    int changes = sqlite_dal_exec_p(store->dal,
        "UPDATE goals SET summary = ?, updated_at = ? WHERE id = ?;",
        bind_text_int64_text, &params);
    return (changes > 0) ? 0 : -1;
}

int goal_store_update_supervisor(goal_store_t *store, const char *id,
                                  pid_t pid, int64_t started_at) {
    if (!store || !id) return -1;
    BindIntInt64Int64Text params = { (int)pid, started_at, time(NULL), id };
    int changes = sqlite_dal_exec_p(store->dal,
        "UPDATE goals SET supervisor_pid = ?, supervisor_started_at = ?, updated_at = ? WHERE id = ?;",
        bind_int_int64_int64_text, &params);
    return (changes > 0) ? 0 : -1;
}

Goal **goal_store_list_all(goal_store_t *store, size_t *count) {
    if (!store || !count) return NULL;
    *count = 0;

    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list(store->dal,
        "SELECT " GOAL_COLUMNS " FROM goals ORDER BY created_at;",
        map_goal, (sqlite_item_free_t)goal_free, NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Goal **)items;
}

Goal **goal_store_list_by_status(goal_store_t *store, GoalStatus status, size_t *count) {
    if (!store || !count) return NULL;
    *count = 0;

    void **items = NULL;
    size_t c = 0;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT " GOAL_COLUMNS " FROM goals WHERE status = ? ORDER BY created_at;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return NULL; }

    sqlite3_bind_int(stmt, 1, (int)status);

    size_t capacity = 8;
    items = malloc(capacity * sizeof(void *));
    if (!items) { sqlite3_finalize(stmt); sqlite_dal_unlock(store->dal); return NULL; }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        void *item = map_goal(stmt, NULL);
        if (!item) continue;
        if (c >= capacity) {
            capacity *= 2;
            void **new_items = realloc(items, capacity * sizeof(void *));
            if (!new_items) { goal_free(item); break; }
            items = new_items;
        }
        items[c++] = item;
    }

    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    if (c == 0) { free(items); return NULL; }
    *count = c;
    return (Goal **)items;
}

void goal_free(Goal *goal) {
    if (!goal) return;
    free(goal->description);
    free(goal->goal_state);
    free(goal->world_state);
    free(goal->summary);
    free(goal);
}

void goal_free_list(Goal **goals, size_t count) {
    if (!goals) return;
    for (size_t i = 0; i < count; i++) goal_free(goals[i]);
    free(goals);
}

const char *goal_status_to_string(GoalStatus status) {
    switch (status) {
        case GOAL_STATUS_PLANNING:  return "planning";
        case GOAL_STATUS_ACTIVE:    return "active";
        case GOAL_STATUS_PAUSED:    return "paused";
        case GOAL_STATUS_COMPLETED: return "completed";
        case GOAL_STATUS_FAILED:    return "failed";
        default:                    return "planning";
    }
}

GoalStatus goal_status_from_string(const char *status_str) {
    if (!status_str) return GOAL_STATUS_PLANNING;
    if (strcmp(status_str, "active") == 0) return GOAL_STATUS_ACTIVE;
    if (strcmp(status_str, "paused") == 0) return GOAL_STATUS_PAUSED;
    if (strcmp(status_str, "completed") == 0) return GOAL_STATUS_COMPLETED;
    if (strcmp(status_str, "failed") == 0) return GOAL_STATUS_FAILED;
    return GOAL_STATUS_PLANNING;
}
