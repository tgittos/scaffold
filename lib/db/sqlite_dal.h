/*
 * sqlite_dal.h - SQLite Data Access Layer
 *
 * Provides common infrastructure for SQLite-backed stores:
 * - Database lifecycle management (open, close, pragmas)
 * - Mutex-protected access
 * - Schema initialization
 * - Common query patterns
 */

#ifndef SQLITE_DAL_H
#define SQLITE_DAL_H

#include <sqlite3.h>
#include <pthread.h>
#include <stddef.h>

/*
 * Opaque handle for a SQLite store.
 * Encapsulates database connection, mutex, and path management.
 */
typedef struct sqlite_dal sqlite_dal_t;

/*
 * Configuration for creating a new SQLite store.
 */
typedef struct {
    const char *db_path;      /* Path to database file (NULL for default) */
    const char *default_name; /* Default filename if db_path is NULL */
    const char *schema_sql;   /* SQL to initialize schema (CREATE TABLE IF NOT EXISTS...) */
    int enable_wal;           /* Enable WAL mode (default: 1) */
    int enable_foreign_keys;  /* Enable foreign key enforcement (default: 1) */
    int busy_timeout_ms;      /* Busy timeout in milliseconds (default: 5000) */
} sqlite_dal_config_t;

/*
 * Default configuration initializer.
 * Use: sqlite_dal_config_t cfg = SQLITE_DAL_CONFIG_DEFAULT;
 */
#define SQLITE_DAL_CONFIG_DEFAULT { \
    .db_path = NULL, \
    .default_name = NULL, \
    .schema_sql = NULL, \
    .enable_wal = 1, \
    .enable_foreign_keys = 1, \
    .busy_timeout_ms = 5000 \
}

/*
 * Create a new SQLite store with the given configuration.
 *
 * Returns: New store handle, or NULL on failure.
 */
sqlite_dal_t *sqlite_dal_create(const sqlite_dal_config_t *config);

/*
 * Increment the reference count on a DAL.
 * Use when sharing a DAL across multiple stores.
 * Returns the same dal pointer for convenience.
 *
 * Thread safety: retain/destroy calls must not race with the final
 * destroy that closes the DB. Typical usage is single-threaded setup
 * and teardown with concurrent query access in between.
 */
sqlite_dal_t *sqlite_dal_retain(sqlite_dal_t *dal);

/*
 * Apply additional schema SQL to an existing DAL.
 * Used by stores created with a shared DAL to initialize their tables.
 * Returns: 0 on success, -1 on failure.
 */
int sqlite_dal_apply_schema(sqlite_dal_t *dal, const char *schema_sql);

/*
 * Destroy a SQLite store and release all resources.
 * With ref counting, the DB is only closed when the last reference is released.
 * Safe to call with NULL.
 */
void sqlite_dal_destroy(sqlite_dal_t *dal);

/*
 * Get the database path.
 * Returns internal string - do not free.
 */
const char *sqlite_dal_get_path(const sqlite_dal_t *dal);

/*
 * Get the raw sqlite3 handle.
 * IMPORTANT: Caller must hold the lock (use sqlite_dal_lock first).
 */
sqlite3 *sqlite_dal_get_db(sqlite_dal_t *dal);

/*
 * Lock the store for exclusive access.
 * Must be paired with sqlite_dal_unlock.
 */
void sqlite_dal_lock(sqlite_dal_t *dal);

/*
 * Unlock the store after exclusive access.
 */
void sqlite_dal_unlock(sqlite_dal_t *dal);

/*
 * Execute a SQL statement without returning results.
 * Acquires lock internally.
 *
 * Returns: 0 on success, -1 on failure.
 */
int sqlite_dal_exec(sqlite_dal_t *dal, const char *sql);

/*
 * Execute a SQL statement with a single int64 parameter.
 * Useful for simple DELETE/UPDATE statements.
 * Acquires lock internally.
 *
 * Returns: Number of rows affected, or -1 on failure.
 */
int sqlite_dal_exec_int64(sqlite_dal_t *dal, const char *sql, int64_t param);

/*
 * Check if a row exists matching a query with a single text parameter.
 * Query should be "SELECT 1 FROM ... WHERE ... LIMIT 1".
 * Acquires lock internally.
 *
 * Returns: 1 if exists, 0 if not, -1 on error.
 */
int sqlite_dal_exists_text(sqlite_dal_t *dal, const char *sql, const char *param);

/*
 * Row mapper callback type.
 * Called for each row in a query result.
 *
 * Parameters:
 *   stmt - The prepared statement positioned at a row
 *   user_data - User-provided context
 *
 * Returns: Newly allocated item to add to results, or NULL to skip.
 */
typedef void *(*sqlite_row_mapper_t)(sqlite3_stmt *stmt, void *user_data);

/*
 * Item destructor callback type.
 * Called to free items on error during query.
 */
typedef void (*sqlite_item_free_t)(void *item);

/*
 * Query for multiple rows with a mapper function.
 * Acquires lock internally.
 *
 * Parameters:
 *   dal - The store handle
 *   sql - The SQL query
 *   mapper - Function to convert each row to an item
 *   item_free - Function to free items on error (can be NULL)
 *   user_data - Passed to mapper
 *   out_items - Output array of items (caller frees)
 *   out_count - Output count of items
 *
 * Returns: 0 on success, -1 on failure.
 */
int sqlite_dal_query_list(sqlite_dal_t *dal, const char *sql,
                          sqlite_row_mapper_t mapper, sqlite_item_free_t item_free,
                          void *user_data, void ***out_items, size_t *out_count);

/*
 * Query for a single row with a mapper function.
 * Acquires lock internally.
 *
 * Parameters:
 *   dal - The store handle
 *   sql - The SQL query (should return at most one row)
 *   mapper - Function to convert the row to an item
 *   user_data - Passed to mapper
 *
 * Returns: Mapped item, or NULL if not found or on error.
 */
void *sqlite_dal_query_one(sqlite_dal_t *dal, const char *sql,
                           sqlite_row_mapper_t mapper, void *user_data);

/*
 * Helper: Get the number of changes from the last statement.
 * Must be called while holding the lock.
 */
int sqlite_dal_changes(sqlite_dal_t *dal);

/*
 * Delete the database file at the given path.
 * Utility for test cleanup.
 */
void sqlite_dal_delete_file(const char *db_path);

/*
 * Parameter binder callback type.
 * Called to bind parameters to a prepared statement before execution.
 *
 * Parameters:
 *   stmt - The prepared statement to bind parameters to
 *   user_data - User-provided context (typically contains the values to bind)
 *
 * Returns: 0 on success, -1 on failure.
 */
typedef int (*sqlite_param_binder_t)(sqlite3_stmt *stmt, void *user_data);

/*
 * Query for multiple rows with parameter binding and a mapper function.
 * Acquires lock internally.
 *
 * Parameters:
 *   dal - The store handle
 *   sql - The SQL query with ? placeholders
 *   binder - Function to bind parameters (can be NULL for no params)
 *   binder_data - Passed to binder
 *   mapper - Function to convert each row to an item
 *   item_free - Function to free items on error (can be NULL)
 *   mapper_data - Passed to mapper
 *   out_items - Output array of items (caller frees)
 *   out_count - Output count of items
 *
 * Returns: 0 on success, -1 on failure.
 */
int sqlite_dal_query_list_p(sqlite_dal_t *dal, const char *sql,
                            sqlite_param_binder_t binder, void *binder_data,
                            sqlite_row_mapper_t mapper, sqlite_item_free_t item_free,
                            void *mapper_data, void ***out_items, size_t *out_count);

/*
 * Query for a single row with parameter binding and a mapper function.
 * Acquires lock internally.
 *
 * Parameters:
 *   dal - The store handle
 *   sql - The SQL query with ? placeholders
 *   binder - Function to bind parameters (can be NULL for no params)
 *   binder_data - Passed to binder
 *   mapper - Function to convert the row to an item
 *   mapper_data - Passed to mapper
 *
 * Returns: Mapped item, or NULL if not found or on error.
 */
void *sqlite_dal_query_one_p(sqlite_dal_t *dal, const char *sql,
                             sqlite_param_binder_t binder, void *binder_data,
                             sqlite_row_mapper_t mapper, void *mapper_data);

/*
 * Execute a parameterized SQL statement.
 * Acquires lock internally.
 *
 * Parameters:
 *   dal - The store handle
 *   sql - The SQL statement with ? placeholders
 *   binder - Function to bind parameters
 *   binder_data - Passed to binder
 *
 * Returns: Number of rows affected, or -1 on failure.
 */
int sqlite_dal_exec_p(sqlite_dal_t *dal, const char *sql,
                      sqlite_param_binder_t binder, void *binder_data);

/*
 * Check if a row exists with parameter binding.
 * Query should be "SELECT 1 FROM ... WHERE ... LIMIT 1".
 * Acquires lock internally.
 *
 * Returns: 1 if exists, 0 if not, -1 on error.
 */
int sqlite_dal_exists_p(sqlite_dal_t *dal, const char *sql,
                        sqlite_param_binder_t binder, void *binder_data);

/*
 * Begin a transaction.
 * Returns: 0 on success, -1 on failure.
 */
int sqlite_dal_begin(sqlite_dal_t *dal);

/*
 * Commit a transaction.
 * Returns: 0 on success, -1 on failure.
 */
int sqlite_dal_commit(sqlite_dal_t *dal);

/*
 * Rollback a transaction.
 * Returns: 0 on success, -1 on failure.
 */
int sqlite_dal_rollback(sqlite_dal_t *dal);

/*
 * Transaction functions for use when caller already holds the lock.
 * Use these when you need to hold the lock across the entire transaction:
 *
 *   sqlite_dal_lock(dal);
 *   sqlite_dal_begin_unlocked(dal);
 *   // ... do work with direct sqlite access ...
 *   sqlite_dal_commit_unlocked(dal);
 *   sqlite_dal_unlock(dal);
 */
int sqlite_dal_begin_unlocked(sqlite_dal_t *dal);
int sqlite_dal_commit_unlocked(sqlite_dal_t *dal);
int sqlite_dal_rollback_unlocked(sqlite_dal_t *dal);

/*
 * Execute SQL without acquiring the lock.
 * IMPORTANT: Caller must hold the lock.
 */
int sqlite_dal_exec_unlocked(sqlite_dal_t *dal, const char *sql);

/*
 * Common binder helper types.
 * These reduce boilerplate for simple parameter binding patterns.
 */
typedef struct { const char *s1; } BindText1;
typedef struct { const char *s1; const char *s2; } BindText2;
typedef struct { const char *s1; int i1; } BindTextInt;
typedef struct { const char *s1; const char *s2; int i1; } BindText2Int;
typedef struct { int i1; } BindInt;
typedef struct { int64_t i1; } BindInt64;

/*
 * Common binder functions.
 * Use with sqlite_dal_*_p functions.
 */
static inline int bind_text1(sqlite3_stmt *stmt, void *data) {
    BindText1 *b = data;
    sqlite3_bind_text(stmt, 1, b->s1, -1, SQLITE_STATIC);
    return 0;
}

static inline int bind_text2(sqlite3_stmt *stmt, void *data) {
    BindText2 *b = data;
    sqlite3_bind_text(stmt, 1, b->s1, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, b->s2, -1, SQLITE_STATIC);
    return 0;
}

static inline int bind_text_int(sqlite3_stmt *stmt, void *data) {
    BindTextInt *b = data;
    sqlite3_bind_text(stmt, 1, b->s1, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, b->i1);
    return 0;
}

static inline int bind_text2_int(sqlite3_stmt *stmt, void *data) {
    BindText2Int *b = data;
    sqlite3_bind_text(stmt, 1, b->s1, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, b->s2, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, b->i1);
    return 0;
}

static inline int bind_int(sqlite3_stmt *stmt, void *data) {
    BindInt *b = data;
    sqlite3_bind_int(stmt, 1, b->i1);
    return 0;
}

static inline int bind_int64(sqlite3_stmt *stmt, void *data) {
    BindInt64 *b = data;
    sqlite3_bind_int64(stmt, 1, b->i1);
    return 0;
}

#endif /* SQLITE_DAL_H */
