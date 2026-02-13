/**
 * lib/workflow/workflow.c - Workflow and task queue implementation
 *
 * Implements work queues using SQLite for persistence.
 */

#include "workflow.h"
#include "../util/app_home.h"
#include "util/uuid_utils.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * WORK QUEUE IMPLEMENTATION
 * ============================================================================= */

struct WorkQueue {
    char name[64];
    sqlite3* db;
};

static int init_work_queue_schema(sqlite3* db) {
    const char* schema =
        "CREATE TABLE IF NOT EXISTS work_items ("
        "  id TEXT PRIMARY KEY,"
        "  queue_name TEXT NOT NULL,"
        "  task_description TEXT NOT NULL,"
        "  context TEXT,"
        "  assigned_to TEXT,"
        "  status INTEGER NOT NULL DEFAULT 0,"
        "  attempt_count INTEGER NOT NULL DEFAULT 0,"
        "  max_attempts INTEGER NOT NULL DEFAULT 3,"
        "  created_at INTEGER NOT NULL,"
        "  assigned_at INTEGER,"
        "  completed_at INTEGER,"
        "  result TEXT,"
        "  error TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_work_items_queue_status "
        "ON work_items(queue_name, status);"
        "CREATE INDEX IF NOT EXISTS idx_work_items_assigned "
        "ON work_items(assigned_to, status);";

    char* err_msg = NULL;
    int rc = sqlite3_exec(db, schema, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

WorkQueue* work_queue_create(const char* name) {
    if (name == NULL || strlen(name) == 0 || strlen(name) >= 64) {
        return NULL;
    }

    WorkQueue* queue = malloc(sizeof(WorkQueue));
    if (queue == NULL) {
        return NULL;
    }

    strncpy(queue->name, name, sizeof(queue->name) - 1);
    queue->name[sizeof(queue->name) - 1] = '\0';

    /* Open database in ralph home directory */
    char db_path[512];
    const char* home = app_home_get();
    snprintf(db_path, sizeof(db_path), "%s/work_queues.db", home);

    int rc = sqlite3_open(db_path, &queue->db);
    if (rc != SQLITE_OK) {
        free(queue);
        return NULL;
    }

    if (init_work_queue_schema(queue->db) != 0) {
        sqlite3_close(queue->db);
        free(queue);
        return NULL;
    }

    return queue;
}

void work_queue_destroy(WorkQueue* queue) {
    if (queue == NULL) {
        return;
    }
    if (queue->db != NULL) {
        sqlite3_close(queue->db);
    }
    free(queue);
}

int work_queue_enqueue(WorkQueue* queue, const char* task_description,
                       const char* context, int max_attempts, char* out_id) {
    if (queue == NULL || task_description == NULL || out_id == NULL) {
        return -1;
    }

    if (max_attempts <= 0) {
        max_attempts = 3;
    }

    /* Generate UUID for work item */
    char uuid[40];
    if (uuid_generate_v4(uuid) != 0) {
        return -1;
    }
    strncpy(out_id, uuid, 39);
    out_id[39] = '\0';

    const char* sql =
        "INSERT INTO work_items "
        "(id, queue_name, task_description, context, status, max_attempts, created_at) "
        "VALUES (?, ?, ?, ?, 0, ?, ?)";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    time_t now = time(NULL);
    sqlite3_bind_text(stmt, 1, uuid, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, queue->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, task_description, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, context, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, max_attempts);
    sqlite3_bind_int64(stmt, 6, (int64_t)now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

WorkItem* work_queue_claim(WorkQueue* queue, const char* worker_id) {
    if (queue == NULL || worker_id == NULL) {
        return NULL;
    }

    /* Claim the oldest pending item */
    const char* update_sql =
        "UPDATE work_items SET "
        "  assigned_to = ?, "
        "  status = 1, "
        "  attempt_count = attempt_count + 1, "
        "  assigned_at = ? "
        "WHERE id = ("
        "  SELECT id FROM work_items "
        "  WHERE queue_name = ? AND status = 0 "
        "  ORDER BY created_at ASC LIMIT 1"
        ") RETURNING *";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, update_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return NULL;
    }

    time_t now = time(NULL);
    sqlite3_bind_text(stmt, 1, worker_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (int64_t)now);
    sqlite3_bind_text(stmt, 3, queue->name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    WorkItem* item = calloc(1, sizeof(WorkItem));
    if (item == NULL) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    /* Extract row data */
    const char* id = (const char*)sqlite3_column_text(stmt, 0);
    const char* qname = (const char*)sqlite3_column_text(stmt, 1);
    const char* desc = (const char*)sqlite3_column_text(stmt, 2);
    const char* ctx = (const char*)sqlite3_column_text(stmt, 3);
    const char* assigned = (const char*)sqlite3_column_text(stmt, 4);

    if (id) strncpy(item->id, id, sizeof(item->id) - 1);
    if (qname) strncpy(item->queue_name, qname, sizeof(item->queue_name) - 1);
    if (assigned) strncpy(item->assigned_to, assigned, sizeof(item->assigned_to) - 1);

    /* Allocate strings with proper error handling */
    if (desc) {
        item->task_description = strdup(desc);
        if (item->task_description == NULL) {
            sqlite3_finalize(stmt);
            free(item);
            return NULL;
        }
    }
    if (ctx) {
        item->context = strdup(ctx);
        if (item->context == NULL) {
            sqlite3_finalize(stmt);
            free(item->task_description);
            free(item);
            return NULL;
        }
    }

    item->status = (WorkItemStatus)sqlite3_column_int(stmt, 5);
    item->attempt_count = sqlite3_column_int(stmt, 6);
    item->max_attempts = sqlite3_column_int(stmt, 7);
    item->created_at = (time_t)sqlite3_column_int64(stmt, 8);
    item->assigned_at = (time_t)sqlite3_column_int64(stmt, 9);

    sqlite3_finalize(stmt);
    return item;
}

int work_queue_complete(WorkQueue* queue, const char* item_id, const char* result) {
    if (queue == NULL || item_id == NULL) {
        return -1;
    }

    const char* sql =
        "UPDATE work_items SET "
        "  status = 2, "
        "  completed_at = ?, "
        "  result = ? "
        "WHERE id = ? AND queue_name = ?";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    time_t now = time(NULL);
    sqlite3_bind_int64(stmt, 1, (int64_t)now);
    sqlite3_bind_text(stmt, 2, result, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, item_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, queue->name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE && sqlite3_changes(queue->db) > 0) ? 0 : -1;
}

int work_queue_fail(WorkQueue* queue, const char* item_id, const char* error) {
    if (queue == NULL || item_id == NULL) {
        return -1;
    }

    /* Check if we should retry or mark as failed */
    const char* check_sql =
        "SELECT attempt_count, max_attempts FROM work_items "
        "WHERE id = ? AND queue_name = ?";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, check_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, item_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, queue->name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int attempt_count = sqlite3_column_int(stmt, 0);
    int max_attempts = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);

    /* Determine new status: retry (back to pending) or failed */
    int new_status = (attempt_count < max_attempts) ? 0 : 3;
    const char* update_sql =
        "UPDATE work_items SET "
        "  status = ?, "
        "  assigned_to = NULL, "
        "  error = ? "
        "WHERE id = ? AND queue_name = ?";

    rc = sqlite3_prepare_v2(queue->db, update_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int(stmt, 1, new_status);
    sqlite3_bind_text(stmt, 2, error, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, item_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, queue->name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int work_queue_pending_count(WorkQueue* queue) {
    if (queue == NULL) {
        return -1;
    }

    const char* sql =
        "SELECT COUNT(*) FROM work_items "
        "WHERE queue_name = ? AND status = 0";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, queue->name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int count = (rc == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);

    return count;
}

int work_queue_remove(WorkQueue* queue, const char* item_id) {
    if (queue == NULL || item_id == NULL) {
        return -1;
    }

    const char* sql =
        "DELETE FROM work_items WHERE id = ? AND queue_name = ?";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, item_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, queue->name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE && sqlite3_changes(queue->db) > 0) ? 0 : -1;
}

WorkItem* work_queue_get_item(WorkQueue* queue, const char* item_id) {
    if (queue == NULL || item_id == NULL) return NULL;

    const char* sql =
        "SELECT id, queue_name, task_description, context, assigned_to, "
        "status, attempt_count, max_attempts, created_at, assigned_at, "
        "completed_at, result, error "
        "FROM work_items WHERE id = ? AND queue_name = ?";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, item_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, queue->name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    WorkItem* item = calloc(1, sizeof(WorkItem));
    if (item == NULL) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    const char* id_val = (const char*)sqlite3_column_text(stmt, 0);
    const char* qname = (const char*)sqlite3_column_text(stmt, 1);
    const char* desc = (const char*)sqlite3_column_text(stmt, 2);
    const char* ctx = (const char*)sqlite3_column_text(stmt, 3);
    const char* assigned = (const char*)sqlite3_column_text(stmt, 4);
    const char* result_val = (const char*)sqlite3_column_text(stmt, 11);
    const char* error_val = (const char*)sqlite3_column_text(stmt, 12);

    if (id_val) strncpy(item->id, id_val, sizeof(item->id) - 1);
    if (qname) strncpy(item->queue_name, qname, sizeof(item->queue_name) - 1);
    if (assigned) strncpy(item->assigned_to, assigned, sizeof(item->assigned_to) - 1);

    if (desc) item->task_description = strdup(desc);
    if (ctx) item->context = strdup(ctx);
    if (result_val) item->result = strdup(result_val);
    if (error_val) item->error = strdup(error_val);

    item->status = (WorkItemStatus)sqlite3_column_int(stmt, 5);
    item->attempt_count = sqlite3_column_int(stmt, 6);
    item->max_attempts = sqlite3_column_int(stmt, 7);
    item->created_at = (time_t)sqlite3_column_int64(stmt, 8);
    item->assigned_at = (time_t)sqlite3_column_int64(stmt, 9);
    item->completed_at = (time_t)sqlite3_column_int64(stmt, 10);

    sqlite3_finalize(stmt);
    return item;
}

void work_item_free(WorkItem* item) {
    if (item == NULL) {
        return;
    }
    free(item->task_description);
    free(item->context);
    free(item->result);
    free(item->error);
    free(item);
}

