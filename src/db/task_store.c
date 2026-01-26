#include "task_store.h"
#include "../utils/uuid_utils.h"
#include "../utils/ptrarray.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>

PTRARRAY_DEFINE(TaskArray, Task)

struct task_store {
    sqlite3* db;
    pthread_mutex_t mutex;
    char* db_path;
};

static task_store_t* g_store_instance = NULL;
static pthread_once_t g_store_once = PTHREAD_ONCE_INIT;

// Schema creation SQL
static const char* SCHEMA_SQL =
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

// Forward declarations
static int init_schema(sqlite3* db);
static char* get_default_db_path(void);
static Task* row_to_task(sqlite3_stmt* stmt);

// Create task store with custom path
task_store_t* task_store_create(const char* db_path) {
    task_store_t* store = calloc(1, sizeof(task_store_t));
    if (store == NULL) {
        return NULL;
    }

    if (db_path != NULL) {
        store->db_path = strdup(db_path);
    } else {
        store->db_path = get_default_db_path();
    }

    if (store->db_path == NULL) {
        free(store);
        return NULL;
    }

    // Initialize mutex
    if (pthread_mutex_init(&store->mutex, NULL) != 0) {
        free(store->db_path);
        free(store);
        return NULL;
    }

    // Open database with foreign keys enabled
    int rc = sqlite3_open(store->db_path, &store->db);
    if (rc != SQLITE_OK) {
        pthread_mutex_destroy(&store->mutex);
        free(store->db_path);
        free(store);
        return NULL;
    }

    // Enable foreign keys
    sqlite3_exec(store->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    // Initialize schema
    if (init_schema(store->db) != 0) {
        sqlite3_close(store->db);
        pthread_mutex_destroy(&store->mutex);
        free(store->db_path);
        free(store);
        return NULL;
    }

    return store;
}

static void create_store_instance(void) {
    g_store_instance = task_store_create(NULL);
}

task_store_t* task_store_get_instance(void) {
    pthread_once(&g_store_once, create_store_instance);
    return g_store_instance;
}

void task_store_destroy(task_store_t* store) {
    if (store == NULL) {
        return;
    }

    pthread_mutex_lock(&store->mutex);
    if (store->db != NULL) {
        sqlite3_close(store->db);
        store->db = NULL;
    }
    pthread_mutex_unlock(&store->mutex);

    pthread_mutex_destroy(&store->mutex);
    free(store->db_path);
    free(store);
}

/**
 * Reset the singleton instance for testing purposes.
 *
 * WARNING: This function is ONLY safe to call in single-threaded test contexts.
 * It is NOT safe to call while task_store_get_instance() may be called from
 * another thread. The pthread_once reset is also non-portable and may not
 * work correctly on all platforms.
 */
void task_store_reset_instance(void) {
    if (g_store_instance != NULL) {
        task_store_destroy(g_store_instance);
        g_store_instance = NULL;
    }
    // Reset pthread_once for testing
    g_store_once = (pthread_once_t)PTHREAD_ONCE_INIT;

    // Delete the database file to ensure clean state for tests
    const char* home = getenv("HOME");
    if (home != NULL) {
        char db_path[512];
        snprintf(db_path, sizeof(db_path), "%s/.local/ralph/tasks.db", home);
        unlink(db_path);
    }
}

// Helper functions
static char* get_default_db_path(void) {
    const char* home = getenv("HOME");
    if (home == NULL) {
        return NULL;
    }

    // Create ~/.local/ralph directory if needed
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/.local", home);
    mkdir(dir_path, 0755);
    snprintf(dir_path, sizeof(dir_path), "%s/.local/ralph", home);
    mkdir(dir_path, 0755);

    size_t path_len = strlen(home) + 32;
    char* db_path = malloc(path_len);
    if (db_path == NULL) {
        return NULL;
    }

    snprintf(db_path, path_len, "%s/.local/ralph/tasks.db", home);
    return db_path;
}

static int init_schema(sqlite3* db) {
    char* err_msg = NULL;
    int rc = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg != NULL) {
            sqlite3_free(err_msg);
        }
        return -1;
    }
    return 0;
}

static Task* row_to_task(sqlite3_stmt* stmt) {
    Task* task = calloc(1, sizeof(Task));
    if (task == NULL) {
        return NULL;
    }

    const char* id = (const char*)sqlite3_column_text(stmt, 0);
    const char* session_id = (const char*)sqlite3_column_text(stmt, 1);
    const char* parent_id = (const char*)sqlite3_column_text(stmt, 2);
    const char* content = (const char*)sqlite3_column_text(stmt, 3);

    if (id != NULL) {
        strncpy(task->id, id, sizeof(task->id) - 1);
    }
    if (session_id != NULL) {
        strncpy(task->session_id, session_id, sizeof(task->session_id) - 1);
    }
    if (parent_id != NULL) {
        strncpy(task->parent_id, parent_id, sizeof(task->parent_id) - 1);
    }
    if (content != NULL) {
        task->content = strdup(content);
        if (task->content == NULL) {
            free(task);
            return NULL;
        }
    }

    task->status = (TaskStatus)sqlite3_column_int(stmt, 4);
    task->priority = (TaskPriority)sqlite3_column_int(stmt, 5);
    task->created_at = (time_t)sqlite3_column_int64(stmt, 6);
    task->updated_at = (time_t)sqlite3_column_int64(stmt, 7);

    return task;
}

// CRUD operations
int task_store_create_task(task_store_t* store, const char* session_id,
                           const char* content, TaskPriority priority,
                           const char* parent_id, char* out_id) {
    if (store == NULL || session_id == NULL || content == NULL || out_id == NULL) {
        return -1;
    }

    char task_id[40] = {0};
    if (uuid_generate_v4(task_id) != 0) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "INSERT INTO tasks (id, session_id, parent_id, content, status, priority, created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    time_t now = time(NULL);

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
    if (parent_id != NULL && parent_id[0] != '\0') {
        sqlite3_bind_text(stmt, 3, parent_id, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, TASK_STATUS_PENDING);
    sqlite3_bind_int(stmt, 6, priority);
    sqlite3_bind_int64(stmt, 7, now);
    sqlite3_bind_int64(stmt, 8, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (rc != SQLITE_DONE) {
        return -1;
    }

    strcpy(out_id, task_id);
    return 0;
}

Task* task_store_get_task(task_store_t* store, const char* id) {
    if (store == NULL || id == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
                      "FROM tasks WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

    Task* task = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        task = row_to_task(stmt);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return task;
}

int task_store_update_status(task_store_t* store, const char* id, TaskStatus status) {
    if (store == NULL || id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "UPDATE tasks SET status = ?, updated_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_text(stmt, 3, id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int task_store_update_content(task_store_t* store, const char* id, const char* content) {
    if (store == NULL || id == NULL || content == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "UPDATE tasks SET content = ?, updated_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, content, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_text(stmt, 3, id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int task_store_update_priority(task_store_t* store, const char* id, TaskPriority priority) {
    if (store == NULL || id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "UPDATE tasks SET priority = ?, updated_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, priority);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_text(stmt, 3, id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int task_store_delete_task(task_store_t* store, const char* id) {
    if (store == NULL || id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "DELETE FROM tasks WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

// Parent/Child operations
Task** task_store_get_children(task_store_t* store, const char* parent_id, size_t* count) {
    if (store == NULL || parent_id == NULL || count == NULL) {
        return NULL;
    }

    *count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql = "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
                      "FROM tasks WHERE parent_id = ? ORDER BY created_at;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, parent_id, -1, SQLITE_STATIC);

    TaskArray arr;
    if (TaskArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task* task = row_to_task(stmt);
        if (task != NULL) {
            if (TaskArray_push(&arr, task) != 0) {
                task_free(task);
                break;
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        TaskArray_destroy_shallow(&arr);
        return NULL;
    }

    *count = arr.count;
    Task** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;

    return result;
}

Task** task_store_get_subtree(task_store_t* store, const char* root_id, size_t* count) {
    if (store == NULL || root_id == NULL || count == NULL) {
        return NULL;
    }

    *count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "WITH RECURSIVE subtree(id) AS ("
        "    SELECT id FROM tasks WHERE parent_id = ?"
        "    UNION ALL"
        "    SELECT t.id FROM tasks t JOIN subtree s ON t.parent_id = s.id"
        ") "
        "SELECT t.id, t.session_id, t.parent_id, t.content, t.status, t.priority, t.created_at, t.updated_at "
        "FROM tasks t JOIN subtree s ON t.id = s.id ORDER BY t.created_at;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, root_id, -1, SQLITE_STATIC);

    TaskArray arr;
    if (TaskArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task* task = row_to_task(stmt);
        if (task != NULL) {
            if (TaskArray_push(&arr, task) != 0) {
                task_free(task);
                TaskArray_destroy(&arr);
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&store->mutex);
                return NULL;
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        TaskArray_destroy_shallow(&arr);
        return NULL;
    }

    *count = arr.count;
    Task** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;

    return result;
}

int task_store_set_parent(task_store_t* store, const char* task_id, const char* parent_id) {
    if (store == NULL || task_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "UPDATE tasks SET parent_id = ?, updated_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    if (parent_id != NULL && parent_id[0] != '\0') {
        sqlite3_bind_text(stmt, 1, parent_id, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_text(stmt, 3, task_id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

// Dependency operations
static int check_circular_dependency(sqlite3* db, const char* task_id, const char* blocked_by_id) {
    // Check if adding this dependency would create a cycle
    // i.e., check if blocked_by_id eventually depends on task_id
    const char* sql =
        "WITH RECURSIVE dep_chain(id) AS ("
        "    SELECT blocked_by_id FROM task_dependencies WHERE task_id = ?"
        "    UNION"
        "    SELECT td.blocked_by_id FROM task_dependencies td JOIN dep_chain dc ON td.task_id = dc.id"
        ") "
        "SELECT 1 FROM dep_chain WHERE id = ?;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, blocked_by_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, task_id, -1, SQLITE_STATIC);

    int found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    return found ? 1 : 0;
}

int task_store_add_dependency(task_store_t* store, const char* task_id, const char* blocked_by_id) {
    if (store == NULL || task_id == NULL || blocked_by_id == NULL) {
        return -1;
    }

    // Cannot depend on self
    if (strcmp(task_id, blocked_by_id) == 0) {
        return -2;
    }

    pthread_mutex_lock(&store->mutex);

    // Check for circular dependency
    int circular = check_circular_dependency(store->db, task_id, blocked_by_id);
    if (circular != 0) {
        pthread_mutex_unlock(&store->mutex);
        return -2;  // Would create circular dependency
    }

    const char* sql = "INSERT OR IGNORE INTO task_dependencies (task_id, blocked_by_id, created_at) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, blocked_by_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, time(NULL));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int task_store_remove_dependency(task_store_t* store, const char* task_id, const char* blocked_by_id) {
    if (store == NULL || task_id == NULL || blocked_by_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "DELETE FROM task_dependencies WHERE task_id = ? AND blocked_by_id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, blocked_by_id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

char** task_store_get_blockers(task_store_t* store, const char* task_id, size_t* count) {
    if (store == NULL || task_id == NULL || count == NULL) {
        return NULL;
    }

    *count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql = "SELECT blocked_by_id FROM task_dependencies WHERE task_id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);

    StringArray arr;
    if (StringArray_init(&arr, free) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* id = (const char*)sqlite3_column_text(stmt, 0);
        if (id != NULL) {
            char* id_copy = strdup(id);
            if (id_copy != NULL) {
                if (StringArray_push(&arr, id_copy) != 0) {
                    free(id_copy);
                    break;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        StringArray_destroy(&arr);
        return NULL;
    }

    *count = arr.count;
    char** result = arr.data;
    arr.data = NULL;  // Transfer ownership; destructor won't free NULL
    arr.count = 0;
    arr.capacity = 0;
    StringArray_destroy(&arr);  // Safe: data is NULL, just frees the array structure

    return result;
}

char** task_store_get_blocking(task_store_t* store, const char* task_id, size_t* count) {
    if (store == NULL || task_id == NULL || count == NULL) {
        return NULL;
    }

    *count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql = "SELECT task_id FROM task_dependencies WHERE blocked_by_id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);

    StringArray arr;
    if (StringArray_init(&arr, free) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* id = (const char*)sqlite3_column_text(stmt, 0);
        if (id != NULL) {
            char* id_copy = strdup(id);
            if (id_copy != NULL) {
                if (StringArray_push(&arr, id_copy) != 0) {
                    free(id_copy);
                    break;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        StringArray_destroy(&arr);
        return NULL;
    }

    *count = arr.count;
    char** result = arr.data;
    arr.data = NULL;  // Transfer ownership; destructor won't free NULL
    arr.count = 0;
    arr.capacity = 0;
    StringArray_destroy(&arr);  // Safe: data is NULL, just frees the array structure

    return result;
}

int task_store_is_blocked(task_store_t* store, const char* task_id) {
    if (store == NULL || task_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    // A task is blocked if any of its blockers are not completed
    const char* sql =
        "SELECT 1 FROM task_dependencies td "
        "JOIN tasks t ON td.blocked_by_id = t.id "
        "WHERE td.task_id = ? AND t.status != 2 "
        "LIMIT 1;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);

    int blocked = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return blocked;
}

// Query operations
Task** task_store_list_by_session(task_store_t* store, const char* session_id,
                                   int status_filter, size_t* count) {
    if (store == NULL || session_id == NULL || count == NULL) {
        return NULL;
    }

    *count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql_all = "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
                          "FROM tasks WHERE session_id = ? ORDER BY created_at;";
    const char* sql_filtered = "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
                               "FROM tasks WHERE session_id = ? AND status = ? ORDER BY created_at;";

    const char* sql = (status_filter < 0) ? sql_all : sql_filtered;
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
    if (status_filter >= 0) {
        sqlite3_bind_int(stmt, 2, status_filter);
    }

    TaskArray arr;
    if (TaskArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task* task = row_to_task(stmt);
        if (task != NULL) {
            if (TaskArray_push(&arr, task) != 0) {
                task_free(task);
                TaskArray_destroy(&arr);
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&store->mutex);
                return NULL;
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        TaskArray_destroy_shallow(&arr);
        return NULL;
    }

    *count = arr.count;
    Task** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;

    return result;
}

Task** task_store_list_roots(task_store_t* store, const char* session_id, size_t* count) {
    if (store == NULL || session_id == NULL || count == NULL) {
        return NULL;
    }

    *count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql = "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
                      "FROM tasks WHERE session_id = ? AND parent_id IS NULL ORDER BY created_at;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);

    TaskArray arr;
    if (TaskArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task* task = row_to_task(stmt);
        if (task != NULL) {
            if (TaskArray_push(&arr, task) != 0) {
                task_free(task);
                TaskArray_destroy(&arr);
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&store->mutex);
                return NULL;
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        TaskArray_destroy_shallow(&arr);
        return NULL;
    }

    *count = arr.count;
    Task** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;

    return result;
}

Task** task_store_list_ready(task_store_t* store, const char* session_id, size_t* count) {
    if (store == NULL || session_id == NULL || count == NULL) {
        return NULL;
    }

    *count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT id, session_id, parent_id, content, status, priority, created_at, updated_at "
        "FROM tasks "
        "WHERE session_id = ? AND status = 0 "
        "AND id NOT IN ("
        "    SELECT td.task_id FROM task_dependencies td "
        "    JOIN tasks t ON td.blocked_by_id = t.id "
        "    WHERE t.status != 2"
        ") "
        "ORDER BY priority DESC, created_at;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);

    TaskArray arr;
    if (TaskArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task* task = row_to_task(stmt);
        if (task != NULL) {
            if (TaskArray_push(&arr, task) != 0) {
                task_free(task);
                TaskArray_destroy(&arr);
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&store->mutex);
                return NULL;
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        TaskArray_destroy_shallow(&arr);
        return NULL;
    }

    *count = arr.count;
    Task** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;

    return result;
}

int task_store_has_pending(task_store_t* store, const char* session_id) {
    if (store == NULL || session_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "SELECT 1 FROM tasks WHERE session_id = ? AND status IN (0, 1) LIMIT 1;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);

    int has_pending = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return has_pending;
}

// Bulk operations
int task_store_replace_session_tasks(task_store_t* store, const char* session_id,
                                      Task* tasks, size_t count) {
    if (store == NULL || session_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    // Start transaction
    int rc = sqlite3_exec(store->db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    // Delete existing tasks for session
    const char* delete_sql = "DELETE FROM tasks WHERE session_id = ?;";
    sqlite3_stmt* delete_stmt = NULL;
    rc = sqlite3_prepare_v2(store->db, delete_sql, -1, &delete_stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(store->db, "ROLLBACK;", NULL, NULL, NULL);
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(delete_stmt, 1, session_id, -1, SQLITE_STATIC);
    rc = sqlite3_step(delete_stmt);
    sqlite3_finalize(delete_stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(store->db, "ROLLBACK;", NULL, NULL, NULL);
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    // Insert new tasks
    if (tasks != NULL && count > 0) {
        const char* insert_sql = "INSERT INTO tasks (id, session_id, parent_id, content, status, priority, created_at, updated_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* insert_stmt = NULL;
        rc = sqlite3_prepare_v2(store->db, insert_sql, -1, &insert_stmt, NULL);
        if (rc != SQLITE_OK) {
            sqlite3_exec(store->db, "ROLLBACK;", NULL, NULL, NULL);
            pthread_mutex_unlock(&store->mutex);
            return -1;
        }

        time_t now = time(NULL);
        for (size_t i = 0; i < count; i++) {
            char task_id[40];
            // Use provided ID or generate new one
            if (tasks[i].id[0] != '\0' && uuid_is_valid(tasks[i].id)) {
                strcpy(task_id, tasks[i].id);
            } else if (uuid_generate_v4(task_id) != 0) {
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(store->db, "ROLLBACK;", NULL, NULL, NULL);
                pthread_mutex_unlock(&store->mutex);
                return -1;
            }

            sqlite3_bind_text(insert_stmt, 1, task_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 2, session_id, -1, SQLITE_STATIC);
            if (tasks[i].parent_id[0] != '\0') {
                sqlite3_bind_text(insert_stmt, 3, tasks[i].parent_id, -1, SQLITE_STATIC);
            } else {
                sqlite3_bind_null(insert_stmt, 3);
            }
            sqlite3_bind_text(insert_stmt, 4, tasks[i].content, -1, SQLITE_STATIC);
            sqlite3_bind_int(insert_stmt, 5, tasks[i].status);
            sqlite3_bind_int(insert_stmt, 6, tasks[i].priority);
            sqlite3_bind_int64(insert_stmt, 7, tasks[i].created_at > 0 ? tasks[i].created_at : now);
            sqlite3_bind_int64(insert_stmt, 8, now);

            rc = sqlite3_step(insert_stmt);
            if (rc != SQLITE_DONE) {
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(store->db, "ROLLBACK;", NULL, NULL, NULL);
                pthread_mutex_unlock(&store->mutex);
                return -1;
            }
            sqlite3_reset(insert_stmt);
        }
        sqlite3_finalize(insert_stmt);
    }

    // Commit transaction
    rc = sqlite3_exec(store->db, "COMMIT;", NULL, NULL, NULL);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_OK) ? 0 : -1;
}

// Memory management
void task_free(Task* task) {
    if (task == NULL) {
        return;
    }

    free(task->content);

    if (task->blocked_by_ids != NULL) {
        for (size_t i = 0; i < task->blocked_by_count; i++) {
            free(task->blocked_by_ids[i]);
        }
        free(task->blocked_by_ids);
    }

    if (task->blocks_ids != NULL) {
        for (size_t i = 0; i < task->blocks_count; i++) {
            free(task->blocks_ids[i]);
        }
        free(task->blocks_ids);
    }

    free(task);
}

void task_free_list(Task** tasks, size_t count) {
    if (tasks == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        task_free(tasks[i]);
    }
    free(tasks);
}

void task_free_id_list(char** ids, size_t count) {
    if (ids == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(ids[i]);
    }
    free(ids);
}

// Status/Priority conversion helpers
const char* task_status_to_string(TaskStatus status) {
    switch (status) {
        case TASK_STATUS_PENDING:     return "pending";
        case TASK_STATUS_IN_PROGRESS: return "in_progress";
        case TASK_STATUS_COMPLETED:   return "completed";
        default:                      return "pending";
    }
}

TaskStatus task_status_from_string(const char* status_str) {
    if (status_str == NULL) {
        return TASK_STATUS_PENDING;
    }
    if (strcmp(status_str, "in_progress") == 0) {
        return TASK_STATUS_IN_PROGRESS;
    }
    if (strcmp(status_str, "completed") == 0) {
        return TASK_STATUS_COMPLETED;
    }
    return TASK_STATUS_PENDING;
}

const char* task_priority_to_string(TaskPriority priority) {
    switch (priority) {
        case TASK_PRIORITY_LOW:    return "low";
        case TASK_PRIORITY_MEDIUM: return "medium";
        case TASK_PRIORITY_HIGH:   return "high";
        default:                   return "medium";
    }
}

TaskPriority task_priority_from_string(const char* priority_str) {
    if (priority_str == NULL) {
        return TASK_PRIORITY_MEDIUM;
    }
    if (strcmp(priority_str, "low") == 0) {
        return TASK_PRIORITY_LOW;
    }
    if (strcmp(priority_str, "high") == 0) {
        return TASK_PRIORITY_HIGH;
    }
    return TASK_PRIORITY_MEDIUM;
}
