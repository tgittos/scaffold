/*
 * task_store.c - Task store implementation using SQLite DAL
 */

#include "task_store.h"
#include "sqlite_dal.h"
#include "../utils/uuid_utils.h"
#include "../utils/ralph_home.h"
#include "../utils/ptrarray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

PTRARRAY_DEFINE(TaskArray, Task)

struct task_store {
    sqlite_dal_t *dal;
};

static task_store_t *g_store_instance = NULL;
static pthread_once_t g_store_once = PTHREAD_ONCE_INIT;

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS tasks ("
    "    id TEXT PRIMARY KEY,"
    "    session_id TEXT NOT NULL,"
    "    parent_id TEXT,"
    "    content TEXT NOT NULL,"
    "    status INTEGER DEFAULT 0,"
    "    priority INTEGER DEFAULT 2,"
    "    created_at INTEGER NOT NULL,"
    "    updated_at INTEGER NOT NULL,"
    "    FOREIGN KEY (parent_id) REFERENCES tasks(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS task_dependencies ("
    "    task_id TEXT NOT NULL,"
    "    blocked_by_id TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    PRIMARY KEY (task_id, blocked_by_id),"
    "    FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE,"
    "    FOREIGN KEY (blocked_by_id) REFERENCES tasks(id) ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_tasks_session ON tasks(session_id);"
    "CREATE INDEX IF NOT EXISTS idx_tasks_session_status ON tasks(session_id, status);"
    "CREATE INDEX IF NOT EXISTS idx_tasks_parent ON tasks(parent_id);"
    "CREATE INDEX IF NOT EXISTS idx_deps_task ON task_dependencies(task_id);"
    "CREATE INDEX IF NOT EXISTS idx_deps_blocked_by ON task_dependencies(blocked_by_id);";

/* Row mapper */
static void *map_task(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    Task *task = calloc(1, sizeof(Task));
    if (task == NULL) return NULL;

    const char *id = (const char *)sqlite3_column_text(stmt, 0);
    const char *session_id = (const char *)sqlite3_column_text(stmt, 1);
    const char *parent_id = (const char *)sqlite3_column_text(stmt, 2);
    const char *content = (const char *)sqlite3_column_text(stmt, 3);

    if (id) strncpy(task->id, id, sizeof(task->id) - 1);
    if (session_id) strncpy(task->session_id, session_id, sizeof(task->session_id) - 1);
    if (parent_id) strncpy(task->parent_id, parent_id, sizeof(task->parent_id) - 1);
    if (content) {
        task->content = strdup(content);
        if (!task->content) { free(task); return NULL; }
    }

    task->status = (TaskStatus)sqlite3_column_int(stmt, 4);
    task->priority = (TaskPriority)sqlite3_column_int(stmt, 5);
    task->created_at = (time_t)sqlite3_column_int64(stmt, 6);
    task->updated_at = (time_t)sqlite3_column_int64(stmt, 7);
    return task;
}

static void *map_string(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    const char *str = (const char *)sqlite3_column_text(stmt, 0);
    return str ? strdup(str) : NULL;
}

/* Additional binder helpers (common ones are in sqlite_dal.h) */
typedef struct { int i1; int64_t i2; const char *s1; } BindIntInt64Text;
typedef struct { const char *s1; int64_t i1; const char *s2; } BindTextInt64Text;

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
    sqlite3_bind_int64(stmt, 2, b->i1);
    sqlite3_bind_text(stmt, 3, b->s2, -1, SQLITE_STATIC);
    return 0;
}

/* Store lifecycle */
task_store_t *task_store_create(const char *db_path) {
    task_store_t *store = calloc(1, sizeof(task_store_t));
    if (store == NULL) return NULL;

    sqlite_dal_config_t config = SQLITE_DAL_CONFIG_DEFAULT;
    config.db_path = db_path;
    config.default_name = "tasks.db";
    config.schema_sql = SCHEMA_SQL;

    store->dal = sqlite_dal_create(&config);
    if (store->dal == NULL) {
        free(store);
        return NULL;
    }
    return store;
}

static void create_store_instance(void) {
    g_store_instance = task_store_create(NULL);
}

task_store_t *task_store_get_instance(void) {
    pthread_once(&g_store_once, create_store_instance);
    return g_store_instance;
}

void task_store_destroy(task_store_t *store) {
    if (store == NULL) return;
    sqlite_dal_destroy(store->dal);
    free(store);
}

void task_store_reset_instance(void) {
    if (g_store_instance != NULL) {
        const char *path = sqlite_dal_get_path(g_store_instance->dal);
        char *path_copy = path ? strdup(path) : NULL;
        task_store_destroy(g_store_instance);
        g_store_instance = NULL;
        if (path_copy) {
            unlink(path_copy);
            free(path_copy);
        }
    } else {
        char *default_path = ralph_home_path("tasks.db");
        if (default_path) {
            unlink(default_path);
            free(default_path);
        }
    }
    g_store_once = (pthread_once_t)PTHREAD_ONCE_INIT;
}

/* Task operations */
int task_store_create_task(task_store_t *store, const char *session_id,
                           const char *content, TaskPriority priority,
                           const char *parent_id, char *out_id) {
    if (!store || !session_id || !content || !out_id) return -1;

    char task_id[40] = {0};
    if (uuid_generate_v4(task_id) != 0) return -1;

    time_t now = time(NULL);
    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO tasks (id, session_id, parent_id, content, status, priority, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
    if (parent_id && parent_id[0]) sqlite3_bind_text(stmt, 3, parent_id, -1, SQLITE_STATIC);
    else sqlite3_bind_null(stmt, 3);
    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, TASK_STATUS_PENDING);
    sqlite3_bind_int(stmt, 6, priority);
    sqlite3_bind_int64(stmt, 7, now);
    sqlite3_bind_int64(stmt, 8, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    if (rc != SQLITE_DONE) return -1;
    strcpy(out_id, task_id);
    return 0;
}

Task *task_store_get_task(task_store_t *store, const char *id) {
    if (!store || !id) return NULL;
    BindText1 params = { id };
    return sqlite_dal_query_one_p(store->dal,
        "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
        "FROM tasks WHERE id = ?;",
        bind_text1, &params, map_task, NULL);
}

int task_store_update_status(task_store_t *store, const char *id, TaskStatus status) {
    if (!store || !id) return -1;
    BindIntInt64Text params = { status, time(NULL), id };
    int changes = sqlite_dal_exec_p(store->dal,
        "UPDATE tasks SET status = ?, updated_at = ? WHERE id = ?;",
        bind_int_int64_text, &params);
    return (changes > 0) ? 0 : -1;
}

int task_store_update_content(task_store_t *store, const char *id, const char *content) {
    if (!store || !id || !content) return -1;
    BindTextInt64Text params = { content, time(NULL), id };
    int changes = sqlite_dal_exec_p(store->dal,
        "UPDATE tasks SET content = ?, updated_at = ? WHERE id = ?;",
        bind_text_int64_text, &params);
    return (changes > 0) ? 0 : -1;
}

int task_store_update_priority(task_store_t *store, const char *id, TaskPriority priority) {
    if (!store || !id) return -1;
    BindIntInt64Text params = { priority, time(NULL), id };
    int changes = sqlite_dal_exec_p(store->dal,
        "UPDATE tasks SET priority = ?, updated_at = ? WHERE id = ?;",
        bind_int_int64_text, &params);
    return (changes > 0) ? 0 : -1;
}

int task_store_delete_task(task_store_t *store, const char *id) {
    if (!store || !id) return -1;
    BindText1 params = { id };
    int changes = sqlite_dal_exec_p(store->dal, "DELETE FROM tasks WHERE id = ?;",
                                     bind_text1, &params);
    return (changes > 0) ? 0 : -1;
}

Task **task_store_get_children(task_store_t *store, const char *parent_id, size_t *count) {
    if (!store || !parent_id || !count) return NULL;
    *count = 0;

    BindText1 params = { parent_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
        "FROM tasks WHERE parent_id = ? ORDER BY created_at;",
        bind_text1, &params, map_task, (sqlite_item_free_t)task_free, NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Task **)items;
}

Task **task_store_get_subtree(task_store_t *store, const char *root_id, size_t *count) {
    if (!store || !root_id || !count) return NULL;
    *count = 0;

    BindText1 params = { root_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "WITH RECURSIVE subtree(id) AS ("
        "    SELECT id FROM tasks WHERE parent_id = ?"
        "    UNION ALL"
        "    SELECT t.id FROM tasks t JOIN subtree s ON t.parent_id = s.id"
        ") "
        "SELECT t.id, t.session_id, t.parent_id, t.content, t.status, t.priority, t.created_at, t.updated_at "
        "FROM tasks t JOIN subtree s ON t.id = s.id ORDER BY t.created_at;",
        bind_text1, &params, map_task, (sqlite_item_free_t)task_free, NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Task **)items;
}

int task_store_set_parent(task_store_t *store, const char *task_id, const char *parent_id) {
    if (!store || !task_id) return -1;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "UPDATE tasks SET parent_id = ?, updated_at = ? WHERE id = ?;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    if (parent_id && parent_id[0]) sqlite3_bind_text(stmt, 1, parent_id, -1, SQLITE_STATIC);
    else sqlite3_bind_null(stmt, 1);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_text(stmt, 3, task_id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

/* Dependency operations */
int task_store_add_dependency(task_store_t *store, const char *task_id, const char *blocked_by_id) {
    if (!store || !task_id || !blocked_by_id) return -1;
    if (strcmp(task_id, blocked_by_id) == 0) return -2;

    /* Check for circular dependency */
    BindText2 check_params = { blocked_by_id, task_id };
    int exists = sqlite_dal_exists_p(store->dal,
        "WITH RECURSIVE dep_chain(id) AS ("
        "    SELECT blocked_by_id FROM task_dependencies WHERE task_id = ?"
        "    UNION"
        "    SELECT td.blocked_by_id FROM task_dependencies td JOIN dep_chain dc ON td.task_id = dc.id"
        ") "
        "SELECT 1 FROM dep_chain WHERE id = ?;",
        bind_text2, &check_params);
    if (exists != 0) return -2;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO task_dependencies (task_id, blocked_by_id, created_at) VALUES (?, ?, ?);",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, blocked_by_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, time(NULL));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int task_store_remove_dependency(task_store_t *store, const char *task_id, const char *blocked_by_id) {
    if (!store || !task_id || !blocked_by_id) return -1;
    BindText2 params = { task_id, blocked_by_id };
    sqlite_dal_exec_p(store->dal,
        "DELETE FROM task_dependencies WHERE task_id = ? AND blocked_by_id = ?;",
        bind_text2, &params);
    return 0;
}

char **task_store_get_blockers(task_store_t *store, const char *task_id, size_t *count) {
    if (!store || !task_id || !count) return NULL;
    *count = 0;

    BindText1 params = { task_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT blocked_by_id FROM task_dependencies WHERE task_id = ?;",
        bind_text1, &params, map_string, free, NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (char **)items;
}

char **task_store_get_blocking(task_store_t *store, const char *task_id, size_t *count) {
    if (!store || !task_id || !count) return NULL;
    *count = 0;

    BindText1 params = { task_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT task_id FROM task_dependencies WHERE blocked_by_id = ?;",
        bind_text1, &params, map_string, free, NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (char **)items;
}

int task_store_is_blocked(task_store_t *store, const char *task_id) {
    if (!store || !task_id) return -1;
    BindText1 params = { task_id };
    return sqlite_dal_exists_p(store->dal,
        "SELECT 1 FROM task_dependencies td "
        "JOIN tasks t ON td.blocked_by_id = t.id "
        "WHERE td.task_id = ? AND t.status != 2 LIMIT 1;",
        bind_text1, &params);
}

/* List operations */
Task **task_store_list_by_session(task_store_t *store, const char *session_id,
                                   int status_filter, size_t *count) {
    if (!store || !session_id || !count) return NULL;
    *count = 0;

    void **items = NULL;
    size_t c = 0;
    int rc;

    if (status_filter < 0) {
        BindText1 params = { session_id };
        rc = sqlite_dal_query_list_p(store->dal,
            "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
            "FROM tasks WHERE session_id = ? ORDER BY created_at;",
            bind_text1, &params, map_task, (sqlite_item_free_t)task_free, NULL, &items, &c);
    } else {
        BindTextInt params = { session_id, status_filter };
        rc = sqlite_dal_query_list_p(store->dal,
            "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
            "FROM tasks WHERE session_id = ? AND status = ? ORDER BY created_at;",
            bind_text_int, &params, map_task, (sqlite_item_free_t)task_free, NULL, &items, &c);
    }

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Task **)items;
}

Task **task_store_list_roots(task_store_t *store, const char *session_id, size_t *count) {
    if (!store || !session_id || !count) return NULL;
    *count = 0;

    BindText1 params = { session_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
        "FROM tasks WHERE session_id = ? AND parent_id IS NULL ORDER BY created_at;",
        bind_text1, &params, map_task, (sqlite_item_free_t)task_free, NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Task **)items;
}

Task **task_store_list_ready(task_store_t *store, const char *session_id, size_t *count) {
    if (!store || !session_id || !count) return NULL;
    *count = 0;

    BindText1 params = { session_id };
    void **items = NULL;
    size_t c = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
        "FROM tasks WHERE session_id = ? AND status = 0 "
        "AND id NOT IN ("
        "    SELECT td.task_id FROM task_dependencies td "
        "    JOIN tasks t ON td.blocked_by_id = t.id WHERE t.status != 2"
        ") ORDER BY priority DESC, created_at;",
        bind_text1, &params, map_task, (sqlite_item_free_t)task_free, NULL, &items, &c);

    if (rc != 0 || c == 0) return NULL;
    *count = c;
    return (Task **)items;
}

int task_store_has_pending(task_store_t *store, const char *session_id) {
    if (!store || !session_id) return -1;
    BindText1 params = { session_id };
    return sqlite_dal_exists_p(store->dal,
        "SELECT 1 FROM tasks WHERE session_id = ? AND status IN (0, 1) LIMIT 1;",
        bind_text1, &params);
}

int task_store_replace_session_tasks(task_store_t *store, const char *session_id,
                                      Task *tasks, size_t count) {
    if (!store || !session_id) return -1;

    /* Hold lock for entire transaction */
    sqlite_dal_lock(store->dal);

    if (sqlite_dal_begin_unlocked(store->dal) != 0) {
        sqlite_dal_unlock(store->dal);
        return -1;
    }

    sqlite3 *db = sqlite_dal_get_db(store->dal);

    /* Delete existing tasks */
    sqlite3_stmt *del_stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "DELETE FROM tasks WHERE session_id = ?;", -1, &del_stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite_dal_rollback_unlocked(store->dal);
        sqlite_dal_unlock(store->dal);
        return -1;
    }
    sqlite3_bind_text(del_stmt, 1, session_id, -1, SQLITE_STATIC);
    rc = sqlite3_step(del_stmt);
    sqlite3_finalize(del_stmt);
    if (rc != SQLITE_DONE) {
        sqlite_dal_rollback_unlocked(store->dal);
        sqlite_dal_unlock(store->dal);
        return -1;
    }

    if (tasks && count > 0) {
        sqlite3_stmt *stmt = NULL;
        rc = sqlite3_prepare_v2(db,
            "INSERT INTO tasks (id, session_id, parent_id, content, status, priority, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);", -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            sqlite_dal_rollback_unlocked(store->dal);
            sqlite_dal_unlock(store->dal);
            return -1;
        }

        time_t now = time(NULL);
        for (size_t i = 0; i < count; i++) {
            char task_id[40];
            if (tasks[i].id[0] && uuid_is_valid(tasks[i].id)) strcpy(task_id, tasks[i].id);
            else if (uuid_generate_v4(task_id) != 0) {
                sqlite3_finalize(stmt);
                sqlite_dal_rollback_unlocked(store->dal);
                sqlite_dal_unlock(store->dal);
                return -1;
            }

            sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
            if (tasks[i].parent_id[0]) sqlite3_bind_text(stmt, 3, tasks[i].parent_id, -1, SQLITE_STATIC);
            else sqlite3_bind_null(stmt, 3);
            sqlite3_bind_text(stmt, 4, tasks[i].content, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 5, tasks[i].status);
            sqlite3_bind_int(stmt, 6, tasks[i].priority);
            sqlite3_bind_int64(stmt, 7, tasks[i].created_at > 0 ? tasks[i].created_at : now);
            sqlite3_bind_int64(stmt, 8, now);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                sqlite_dal_rollback_unlocked(store->dal);
                sqlite_dal_unlock(store->dal);
                return -1;
            }
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }

    int result = sqlite_dal_commit_unlocked(store->dal);
    sqlite_dal_unlock(store->dal);
    return result;
}

/* Free functions */
void task_free(Task *task) {
    if (!task) return;
    free(task->content);
    if (task->blocked_by_ids) {
        for (size_t i = 0; i < task->blocked_by_count; i++) free(task->blocked_by_ids[i]);
        free(task->blocked_by_ids);
    }
    if (task->blocks_ids) {
        for (size_t i = 0; i < task->blocks_count; i++) free(task->blocks_ids[i]);
        free(task->blocks_ids);
    }
    free(task);
}

void task_free_list(Task **tasks, size_t count) {
    if (!tasks) return;
    for (size_t i = 0; i < count; i++) task_free(tasks[i]);
    free(tasks);
}

void task_free_id_list(char **ids, size_t count) {
    if (!ids) return;
    for (size_t i = 0; i < count; i++) free(ids[i]);
    free(ids);
}

/* Status/priority conversion */
const char *task_status_to_string(TaskStatus status) {
    switch (status) {
        case TASK_STATUS_PENDING:     return "pending";
        case TASK_STATUS_IN_PROGRESS: return "in_progress";
        case TASK_STATUS_COMPLETED:   return "completed";
        default:                      return "pending";
    }
}

TaskStatus task_status_from_string(const char *status_str) {
    if (!status_str) return TASK_STATUS_PENDING;
    if (strcmp(status_str, "in_progress") == 0) return TASK_STATUS_IN_PROGRESS;
    if (strcmp(status_str, "completed") == 0) return TASK_STATUS_COMPLETED;
    return TASK_STATUS_PENDING;
}

const char *task_priority_to_string(TaskPriority priority) {
    switch (priority) {
        case TASK_PRIORITY_LOW:    return "low";
        case TASK_PRIORITY_MEDIUM: return "medium";
        case TASK_PRIORITY_HIGH:   return "high";
        default:                   return "medium";
    }
}

TaskPriority task_priority_from_string(const char *priority_str) {
    if (!priority_str) return TASK_PRIORITY_MEDIUM;
    if (strcmp(priority_str, "low") == 0) return TASK_PRIORITY_LOW;
    if (strcmp(priority_str, "high") == 0) return TASK_PRIORITY_HIGH;
    return TASK_PRIORITY_MEDIUM;
}
