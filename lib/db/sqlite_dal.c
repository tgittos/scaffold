/*
 * sqlite_dal.c - SQLite Data Access Layer implementation
 */

#include "sqlite_dal.h"
#include "../util/app_home.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct sqlite_dal {
    sqlite3 *db;
    pthread_mutex_t mutex;
    char *db_path;
};

static int init_pragmas(sqlite3 *db, const sqlite_dal_config_t *config) {
    if (config->enable_foreign_keys) {
        if (sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) != SQLITE_OK) {
            return -1;
        }
    }

    if (config->enable_wal) {
        if (sqlite3_exec(db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL) != SQLITE_OK) {
            return -1;
        }
    }

    if (config->busy_timeout_ms > 0) {
        sqlite3_busy_timeout(db, config->busy_timeout_ms);
    }

    return 0;
}

static int init_schema(sqlite3 *db, const char *schema_sql) {
    if (schema_sql == NULL || schema_sql[0] == '\0') {
        return 0;
    }

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, schema_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg != NULL) {
            sqlite3_free(err_msg);
        }
        return -1;
    }
    return 0;
}

static char *get_default_path(const char *default_name) {
    if (default_name == NULL) {
        return NULL;
    }

    if (app_home_ensure_exists() != 0) {
        return NULL;
    }

    return app_home_path(default_name);
}

sqlite_dal_t *sqlite_dal_create(const sqlite_dal_config_t *config) {
    if (config == NULL) {
        return NULL;
    }

    sqlite_dal_t *dal = calloc(1, sizeof(sqlite_dal_t));
    if (dal == NULL) {
        return NULL;
    }

    if (config->db_path != NULL) {
        dal->db_path = strdup(config->db_path);
    } else {
        dal->db_path = get_default_path(config->default_name);
    }

    if (dal->db_path == NULL) {
        free(dal);
        return NULL;
    }

    if (pthread_mutex_init(&dal->mutex, NULL) != 0) {
        free(dal->db_path);
        free(dal);
        return NULL;
    }

    int rc = sqlite3_open(dal->db_path, &dal->db);
    if (rc != SQLITE_OK) {
        pthread_mutex_destroy(&dal->mutex);
        free(dal->db_path);
        free(dal);
        return NULL;
    }

    if (init_pragmas(dal->db, config) != 0) {
        sqlite3_close(dal->db);
        pthread_mutex_destroy(&dal->mutex);
        free(dal->db_path);
        free(dal);
        return NULL;
    }

    if (init_schema(dal->db, config->schema_sql) != 0) {
        sqlite3_close(dal->db);
        pthread_mutex_destroy(&dal->mutex);
        free(dal->db_path);
        free(dal);
        return NULL;
    }

    return dal;
}

void sqlite_dal_destroy(sqlite_dal_t *dal) {
    if (dal == NULL) {
        return;
    }

    pthread_mutex_lock(&dal->mutex);
    if (dal->db != NULL) {
        sqlite3_close(dal->db);
        dal->db = NULL;
    }
    pthread_mutex_unlock(&dal->mutex);

    pthread_mutex_destroy(&dal->mutex);
    free(dal->db_path);
    free(dal);
}

const char *sqlite_dal_get_path(const sqlite_dal_t *dal) {
    if (dal == NULL) {
        return NULL;
    }
    return dal->db_path;
}

sqlite3 *sqlite_dal_get_db(sqlite_dal_t *dal) {
    if (dal == NULL) {
        return NULL;
    }
    return dal->db;
}

void sqlite_dal_lock(sqlite_dal_t *dal) {
    if (dal != NULL) {
        pthread_mutex_lock(&dal->mutex);
    }
}

void sqlite_dal_unlock(sqlite_dal_t *dal) {
    if (dal != NULL) {
        pthread_mutex_unlock(&dal->mutex);
    }
}

int sqlite_dal_exec(sqlite_dal_t *dal, const char *sql) {
    if (dal == NULL || sql == NULL) {
        return -1;
    }

    pthread_mutex_lock(&dal->mutex);
    int rc = sqlite3_exec(dal->db, sql, NULL, NULL, NULL);
    pthread_mutex_unlock(&dal->mutex);

    return (rc == SQLITE_OK) ? 0 : -1;
}

int sqlite_dal_exec_int64(sqlite_dal_t *dal, const char *sql, int64_t param) {
    if (dal == NULL || sql == NULL) {
        return -1;
    }

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, param);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(dal->db);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&dal->mutex);

    return (rc == SQLITE_DONE) ? changes : -1;
}

int sqlite_dal_exists_text(sqlite_dal_t *dal, const char *sql, const char *param) {
    if (dal == NULL || sql == NULL || param == NULL) {
        return -1;
    }

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, param, -1, SQLITE_STATIC);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&dal->mutex);

    return exists;
}

int sqlite_dal_query_list(sqlite_dal_t *dal, const char *sql,
                          sqlite_row_mapper_t mapper, sqlite_item_free_t item_free,
                          void *user_data, void ***out_items, size_t *out_count) {
    if (dal == NULL || sql == NULL || mapper == NULL || out_items == NULL || out_count == NULL) {
        return -1;
    }

    *out_items = NULL;
    *out_count = 0;

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    size_t capacity = 16;
    size_t count = 0;
    void **items = malloc(capacity * sizeof(void *));
    if (items == NULL) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        void *item = mapper(stmt, user_data);
        if (item == NULL) {
            continue;
        }

        if (count >= capacity) {
            size_t new_capacity = capacity * 2;
            void **new_items = realloc(items, new_capacity * sizeof(void *));
            if (new_items == NULL) {
                if (item_free != NULL) {
                    item_free(item);
                }
                for (size_t i = 0; i < count; i++) {
                    if (item_free != NULL) {
                        item_free(items[i]);
                    }
                }
                free(items);
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&dal->mutex);
                return -1;
            }
            items = new_items;
            capacity = new_capacity;
        }

        items[count++] = item;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&dal->mutex);

    if (count == 0) {
        free(items);
        return 0;
    }

    *out_items = items;
    *out_count = count;
    return 0;
}

void *sqlite_dal_query_one(sqlite_dal_t *dal, const char *sql,
                           sqlite_row_mapper_t mapper, void *user_data) {
    if (dal == NULL || sql == NULL || mapper == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return NULL;
    }

    void *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = mapper(stmt, user_data);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&dal->mutex);

    return result;
}

int sqlite_dal_changes(sqlite_dal_t *dal) {
    if (dal == NULL || dal->db == NULL) {
        return 0;
    }
    return sqlite3_changes(dal->db);
}

void sqlite_dal_delete_file(const char *db_path) {
    if (db_path != NULL) {
        unlink(db_path);
    }
}

int sqlite_dal_query_list_p(sqlite_dal_t *dal, const char *sql,
                            sqlite_param_binder_t binder, void *binder_data,
                            sqlite_row_mapper_t mapper, sqlite_item_free_t item_free,
                            void *mapper_data, void ***out_items, size_t *out_count) {
    if (dal == NULL || sql == NULL || mapper == NULL || out_items == NULL || out_count == NULL) {
        return -1;
    }

    *out_items = NULL;
    *out_count = 0;

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    if (binder != NULL && binder(stmt, binder_data) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    size_t capacity = 16;
    size_t count = 0;
    void **items = malloc(capacity * sizeof(void *));
    if (items == NULL) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        void *item = mapper(stmt, mapper_data);
        if (item == NULL) {
            continue;
        }

        if (count >= capacity) {
            size_t new_capacity = capacity * 2;
            void **new_items = realloc(items, new_capacity * sizeof(void *));
            if (new_items == NULL) {
                if (item_free != NULL) {
                    item_free(item);
                }
                for (size_t i = 0; i < count; i++) {
                    if (item_free != NULL) {
                        item_free(items[i]);
                    }
                }
                free(items);
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&dal->mutex);
                return -1;
            }
            items = new_items;
            capacity = new_capacity;
        }

        items[count++] = item;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&dal->mutex);

    if (count == 0) {
        free(items);
        return 0;
    }

    *out_items = items;
    *out_count = count;
    return 0;
}

void *sqlite_dal_query_one_p(sqlite_dal_t *dal, const char *sql,
                             sqlite_param_binder_t binder, void *binder_data,
                             sqlite_row_mapper_t mapper, void *mapper_data) {
    if (dal == NULL || sql == NULL || mapper == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return NULL;
    }

    if (binder != NULL && binder(stmt, binder_data) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&dal->mutex);
        return NULL;
    }

    void *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = mapper(stmt, mapper_data);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&dal->mutex);

    return result;
}

int sqlite_dal_exec_p(sqlite_dal_t *dal, const char *sql,
                      sqlite_param_binder_t binder, void *binder_data) {
    if (dal == NULL || sql == NULL) {
        return -1;
    }

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    if (binder != NULL && binder(stmt, binder_data) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(dal->db);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&dal->mutex);

    return (rc == SQLITE_DONE) ? changes : -1;
}

int sqlite_dal_exists_p(sqlite_dal_t *dal, const char *sql,
                        sqlite_param_binder_t binder, void *binder_data) {
    if (dal == NULL || sql == NULL) {
        return -1;
    }

    pthread_mutex_lock(&dal->mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dal->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    if (binder != NULL && binder(stmt, binder_data) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&dal->mutex);
        return -1;
    }

    int exists = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&dal->mutex);

    return exists;
}

int sqlite_dal_begin(sqlite_dal_t *dal) {
    return sqlite_dal_exec(dal, "BEGIN TRANSACTION;");
}

int sqlite_dal_commit(sqlite_dal_t *dal) {
    return sqlite_dal_exec(dal, "COMMIT;");
}

int sqlite_dal_rollback(sqlite_dal_t *dal) {
    return sqlite_dal_exec(dal, "ROLLBACK;");
}

int sqlite_dal_exec_unlocked(sqlite_dal_t *dal, const char *sql) {
    if (dal == NULL || sql == NULL) {
        return -1;
    }

    int rc = sqlite3_exec(dal->db, sql, NULL, NULL, NULL);
    return (rc == SQLITE_OK) ? 0 : -1;
}

int sqlite_dal_begin_unlocked(sqlite_dal_t *dal) {
    return sqlite_dal_exec_unlocked(dal, "BEGIN TRANSACTION;");
}

int sqlite_dal_commit_unlocked(sqlite_dal_t *dal) {
    return sqlite_dal_exec_unlocked(dal, "COMMIT;");
}

int sqlite_dal_rollback_unlocked(sqlite_dal_t *dal) {
    return sqlite_dal_exec_unlocked(dal, "ROLLBACK;");
}
