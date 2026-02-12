/**
 * lib/workflow/workflow.c - Workflow and task queue implementation
 *
 * Implements work queues using SQLite for persistence.
 */

#include "workflow.h"
#include "../util/app_home.h"
#include "../util/executable_path.h"
#include "util/uuid_utils.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

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

/* =============================================================================
 * WORKER MANAGEMENT
 * ============================================================================= */

static char* get_ralph_executable_path(void) {
    return get_executable_path();
}

WorkerHandle* worker_spawn(const char* queue_name, const char* system_prompt) {
    if (queue_name == NULL || strlen(queue_name) == 0) {
        return NULL;
    }

    WorkerHandle* handle = calloc(1, sizeof(WorkerHandle));
    if (handle == NULL) {
        return NULL;
    }

    strncpy(handle->queue_name, queue_name, sizeof(handle->queue_name) - 1);

    /* Generate worker ID */
    char uuid[40];
    if (uuid_generate_v4(uuid) != 0) {
        free(handle);
        return NULL;
    }
    snprintf(handle->agent_id, sizeof(handle->agent_id), "worker-%s", uuid);

    /* Write system prompt to temp file if provided */
    if (system_prompt != NULL && strlen(system_prompt) > 0) {
        snprintf(handle->system_prompt_file, sizeof(handle->system_prompt_file),
                 "/tmp/scaffold_prompt_XXXXXX");
        int fd = mkstemp(handle->system_prompt_file);
        if (fd < 0) {
            free(handle);
            return NULL;
        }
        size_t prompt_len = strlen(system_prompt);
        ssize_t written = write(fd, system_prompt, prompt_len);
        close(fd);
        if (written < 0 || (size_t)written != prompt_len) {
            unlink(handle->system_prompt_file);
            free(handle);
            return NULL;
        }
    }

    /* Get path to ralph executable */
    char* ralph_path = get_ralph_executable_path();
    if (ralph_path == NULL) {
        if (handle->system_prompt_file[0]) {
            unlink(handle->system_prompt_file);
        }
        free(handle);
        return NULL;
    }

    /* Fork worker process */
    pid_t pid = fork();
    if (pid < 0) {
        free(ralph_path);
        if (handle->system_prompt_file[0]) {
            unlink(handle->system_prompt_file);
        }
        free(handle);
        return NULL;
    }

    if (pid == 0) {
        /* Child process - exec as worker */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char* args[10];
        int arg_idx = 0;

        args[arg_idx++] = ralph_path;
        args[arg_idx++] = "--worker";
        args[arg_idx++] = "--queue";
        args[arg_idx++] = (char*)queue_name;

        if (handle->system_prompt_file[0]) {
            args[arg_idx++] = "--system-prompt-file";
            args[arg_idx++] = handle->system_prompt_file;
        }

        args[arg_idx] = NULL;

        execv(ralph_path, args);
        _exit(127);
    }

    /* Parent process */
    free(ralph_path);
    handle->pid = pid;
    handle->is_running = true;

    return handle;
}

bool worker_is_running(WorkerHandle* handle) {
    if (handle == NULL) {
        return false;
    }

    if (!handle->is_running) {
        return false;
    }

    /* Check if process is still alive */
    int status;
    pid_t result = waitpid(handle->pid, &status, WNOHANG);
    if (result == handle->pid) {
        /* Process has terminated */
        handle->is_running = false;
        return false;
    }

    return true;
}

int worker_stop(WorkerHandle* handle) {
    if (handle == NULL || !handle->is_running) {
        return -1;
    }

    /* Send SIGTERM */
    if (kill(handle->pid, SIGTERM) != 0) {
        return -1;
    }

    /* Wait briefly for termination */
    int status;
    pid_t result = waitpid(handle->pid, &status, WNOHANG);
    if (result == 0) {
        /* Still running, send SIGKILL */
        usleep(100000); /* 100ms */
        kill(handle->pid, SIGKILL);
        waitpid(handle->pid, &status, 0);
    }

    handle->is_running = false;
    return 0;
}

void worker_handle_free(WorkerHandle* handle) {
    if (handle == NULL) {
        return;
    }
    if (handle->system_prompt_file[0]) {
        unlink(handle->system_prompt_file);
    }
    free(handle);
}
