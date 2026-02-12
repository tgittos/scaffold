/*
 * test_sqlite_dal.c - Unit tests for SQLite Data Access Layer
 */

#include "../unity/unity.h"
#include "db/sqlite_dal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TEST_DB_PATH = "/tmp/test_sqlite_dal.db";

static const char *TEST_SCHEMA =
    "CREATE TABLE IF NOT EXISTS items ("
    "    id INTEGER PRIMARY KEY,"
    "    name TEXT NOT NULL,"
    "    value INTEGER DEFAULT 0"
    ");";

static sqlite_dal_t *test_dal = NULL;

void setUp(void) {
    unlink(TEST_DB_PATH);

    sqlite_dal_config_t config = SQLITE_DAL_CONFIG_DEFAULT;
    config.db_path = TEST_DB_PATH;
    config.schema_sql = TEST_SCHEMA;

    test_dal = sqlite_dal_create(&config);
}

void tearDown(void) {
    if (test_dal != NULL) {
        sqlite_dal_destroy(test_dal);
        test_dal = NULL;
    }
    unlink(TEST_DB_PATH);
}

/* Test basic creation and destruction */
void test_create_destroy(void) {
    TEST_ASSERT_NOT_NULL(test_dal);
    TEST_ASSERT_EQUAL_STRING(TEST_DB_PATH, sqlite_dal_get_path(test_dal));
}

/* Test creation with NULL config fails */
void test_create_null_config(void) {
    sqlite_dal_t *dal = sqlite_dal_create(NULL);
    TEST_ASSERT_NULL(dal);
}

/* Test creation with default path */
void test_create_default_path(void) {
    sqlite_dal_config_t config = SQLITE_DAL_CONFIG_DEFAULT;
    config.default_name = "test_dal_default.db";
    config.schema_sql = TEST_SCHEMA;

    sqlite_dal_t *dal = sqlite_dal_create(&config);

    /* Default path creation depends on app_home which may fail in test env */
    if (dal != NULL) {
        TEST_ASSERT_NOT_NULL(sqlite_dal_get_path(dal));

        const char *path = sqlite_dal_get_path(dal);
        char *path_copy = strdup(path);
        sqlite_dal_destroy(dal);
        sqlite_dal_delete_file(path_copy);
        free(path_copy);
    }
    /* If dal is NULL, that's OK - app_home doesn't exist in test env */
}

/* Test destroy with NULL is safe */
void test_destroy_null(void) {
    sqlite_dal_destroy(NULL);
}

/* Test exec with simple INSERT */
void test_exec_insert(void) {
    int rc = sqlite_dal_exec(test_dal, "INSERT INTO items (name, value) VALUES ('test', 42);");
    TEST_ASSERT_EQUAL_INT(0, rc);
}

/* Test exec with NULL dal */
void test_exec_null_dal(void) {
    int rc = sqlite_dal_exec(NULL, "SELECT 1;");
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test exec with NULL sql */
void test_exec_null_sql(void) {
    int rc = sqlite_dal_exec(test_dal, NULL);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test exec with invalid SQL */
void test_exec_invalid_sql(void) {
    int rc = sqlite_dal_exec(test_dal, "NOT VALID SQL");
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test exec_int64 for DELETE */
void test_exec_int64_delete(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (1, 'one', 1);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (2, 'two', 2);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (3, 'three', 3);");

    int deleted = sqlite_dal_exec_int64(test_dal, "DELETE FROM items WHERE value > ?;", 1);
    TEST_ASSERT_EQUAL_INT(2, deleted);
}

/* Test exec_int64 with NULL dal */
void test_exec_int64_null_dal(void) {
    int rc = sqlite_dal_exec_int64(NULL, "DELETE FROM items WHERE id = ?;", 1);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test exists_text returns 1 when row exists */
void test_exists_text_found(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (name, value) VALUES ('findme', 100);");

    int exists = sqlite_dal_exists_text(test_dal,
        "SELECT 1 FROM items WHERE name = ? LIMIT 1;", "findme");
    TEST_ASSERT_EQUAL_INT(1, exists);
}

/* Test exists_text returns 0 when row doesn't exist */
void test_exists_text_not_found(void) {
    int exists = sqlite_dal_exists_text(test_dal,
        "SELECT 1 FROM items WHERE name = ? LIMIT 1;", "nonexistent");
    TEST_ASSERT_EQUAL_INT(0, exists);
}

/* Test exists_text with NULL dal */
void test_exists_text_null_dal(void) {
    int rc = sqlite_dal_exists_text(NULL, "SELECT 1;", "test");
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Simple test struct for query tests */
typedef struct {
    int id;
    char *name;
    int value;
} TestItem;

static void test_item_free(void *item) {
    if (item != NULL) {
        TestItem *ti = (TestItem *)item;
        free(ti->name);
        free(ti);
    }
}

static void *test_item_mapper(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;

    TestItem *item = calloc(1, sizeof(TestItem));
    if (item == NULL) {
        return NULL;
    }

    item->id = sqlite3_column_int(stmt, 0);

    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    if (name != NULL) {
        item->name = strdup(name);
        if (item->name == NULL) {
            free(item);
            return NULL;
        }
    }

    item->value = sqlite3_column_int(stmt, 2);

    return item;
}

/* Test query_list with multiple rows */
void test_query_list_multiple(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (1, 'alpha', 10);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (2, 'beta', 20);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (3, 'gamma', 30);");

    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list(test_dal,
        "SELECT id, name, value FROM items ORDER BY id;",
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_NOT_NULL(items);

    TestItem *item0 = (TestItem *)items[0];
    TEST_ASSERT_EQUAL_INT(1, item0->id);
    TEST_ASSERT_EQUAL_STRING("alpha", item0->name);
    TEST_ASSERT_EQUAL_INT(10, item0->value);

    TestItem *item2 = (TestItem *)items[2];
    TEST_ASSERT_EQUAL_INT(3, item2->id);
    TEST_ASSERT_EQUAL_STRING("gamma", item2->name);

    for (size_t i = 0; i < count; i++) {
        test_item_free(items[i]);
    }
    free(items);
}

/* Test query_list with no results */
void test_query_list_empty(void) {
    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list(test_dal,
        "SELECT id, name, value FROM items;",
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(0, count);
    TEST_ASSERT_NULL(items);
}

/* Test query_list with NULL dal */
void test_query_list_null_dal(void) {
    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list(NULL,
        "SELECT id, name, value FROM items;",
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test query_one with existing row */
void test_query_one_found(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (42, 'answer', 100);");

    TestItem *item = sqlite_dal_query_one(test_dal,
        "SELECT id, name, value FROM items WHERE id = 42;",
        test_item_mapper, NULL);

    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_INT(42, item->id);
    TEST_ASSERT_EQUAL_STRING("answer", item->name);
    TEST_ASSERT_EQUAL_INT(100, item->value);

    test_item_free(item);
}

/* Test query_one with no match */
void test_query_one_not_found(void) {
    TestItem *item = sqlite_dal_query_one(test_dal,
        "SELECT id, name, value FROM items WHERE id = 999;",
        test_item_mapper, NULL);

    TEST_ASSERT_NULL(item);
}

/* Test query_one with NULL dal */
void test_query_one_null_dal(void) {
    void *item = sqlite_dal_query_one(NULL,
        "SELECT 1;", test_item_mapper, NULL);
    TEST_ASSERT_NULL(item);
}

/* Test lock/unlock and get_db */
void test_lock_unlock_get_db(void) {
    sqlite_dal_lock(test_dal);

    sqlite3 *db = sqlite_dal_get_db(test_dal);
    TEST_ASSERT_NOT_NULL(db);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "SELECT 1;", -1, &stmt, NULL);
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, rc);
    sqlite3_finalize(stmt);

    sqlite_dal_unlock(test_dal);
}

/* Test lock/unlock with NULL is safe */
void test_lock_unlock_null(void) {
    sqlite_dal_lock(NULL);
    sqlite_dal_unlock(NULL);
}

/* Test get_db with NULL */
void test_get_db_null(void) {
    sqlite3 *db = sqlite_dal_get_db(NULL);
    TEST_ASSERT_NULL(db);
}

/* Test get_path with NULL */
void test_get_path_null(void) {
    const char *path = sqlite_dal_get_path(NULL);
    TEST_ASSERT_NULL(path);
}

/* Test changes */
void test_changes(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (name) VALUES ('a');");
    sqlite_dal_exec(test_dal, "INSERT INTO items (name) VALUES ('b');");

    sqlite_dal_lock(test_dal);
    sqlite3 *db = sqlite_dal_get_db(test_dal);
    sqlite3_exec(db, "UPDATE items SET value = 1;", NULL, NULL, NULL);
    int changes = sqlite_dal_changes(test_dal);
    sqlite_dal_unlock(test_dal);

    TEST_ASSERT_EQUAL_INT(2, changes);
}

/* Test changes with NULL */
void test_changes_null(void) {
    int changes = sqlite_dal_changes(NULL);
    TEST_ASSERT_EQUAL_INT(0, changes);
}

/* Test delete_file */
void test_delete_file(void) {
    const char *tmp_path = "/tmp/test_dal_delete.db";
    sqlite_dal_config_t config = SQLITE_DAL_CONFIG_DEFAULT;
    config.db_path = tmp_path;
    config.schema_sql = TEST_SCHEMA;

    sqlite_dal_t *dal = sqlite_dal_create(&config);
    TEST_ASSERT_NOT_NULL(dal);
    sqlite_dal_destroy(dal);

    TEST_ASSERT_EQUAL_INT(0, access(tmp_path, F_OK));

    sqlite_dal_delete_file(tmp_path);

    TEST_ASSERT_NOT_EQUAL_INT(0, access(tmp_path, F_OK));
}

/* Test delete_file with NULL is safe */
void test_delete_file_null(void) {
    sqlite_dal_delete_file(NULL);
}

/* Test foreign keys are enabled by default */
void test_foreign_keys_enabled(void) {
    sqlite_dal_lock(test_dal);
    sqlite3 *db = sqlite_dal_get_db(test_dal);

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db, "PRAGMA foreign_keys;", -1, &stmt, NULL);
    sqlite3_step(stmt);
    int enabled = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    sqlite_dal_unlock(test_dal);

    TEST_ASSERT_EQUAL_INT(1, enabled);
}

/* Test schema initialization */
void test_schema_initialized(void) {
    sqlite_dal_lock(test_dal);
    sqlite3 *db = sqlite_dal_get_db(test_dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='items';",
        -1, &stmt, NULL);
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, rc);

    int found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(test_dal);

    TEST_ASSERT_EQUAL_INT(1, found);
}

/* Binder for parameterized query tests */
static int bind_name_param(sqlite3_stmt *stmt, void *user_data) {
    const char *name = (const char *)user_data;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    return 0;
}

static int bind_failing_binder(sqlite3_stmt *stmt, void *user_data) {
    (void)stmt;
    (void)user_data;
    return -1;
}

/* Test query_list_p with parameter binding */
void test_query_list_p_with_params(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (1, 'alpha', 10);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (2, 'alpha', 20);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (3, 'beta', 30);");

    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list_p(test_dal,
        "SELECT id, name, value FROM items WHERE name = ? ORDER BY id;",
        bind_name_param, (void *)"alpha",
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_NOT_NULL(items);

    TestItem *item0 = (TestItem *)items[0];
    TEST_ASSERT_EQUAL_INT(1, item0->id);
    TEST_ASSERT_EQUAL_STRING("alpha", item0->name);

    TestItem *item1 = (TestItem *)items[1];
    TEST_ASSERT_EQUAL_INT(2, item1->id);

    for (size_t i = 0; i < count; i++) {
        test_item_free(items[i]);
    }
    free(items);
}

/* Test query_list_p with no binder */
void test_query_list_p_no_binder(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (1, 'test', 10);");

    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list_p(test_dal,
        "SELECT id, name, value FROM items;",
        NULL, NULL,
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, count);

    for (size_t i = 0; i < count; i++) {
        test_item_free(items[i]);
    }
    free(items);
}

/* Test query_list_p with failing binder */
void test_query_list_p_binder_fails(void) {
    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list_p(test_dal,
        "SELECT id, name, value FROM items WHERE name = ?;",
        bind_failing_binder, NULL,
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test query_one_p with parameter binding */
void test_query_one_p_with_params(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (1, 'findme', 100);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (2, 'other', 200);");

    TestItem *item = sqlite_dal_query_one_p(test_dal,
        "SELECT id, name, value FROM items WHERE name = ?;",
        bind_name_param, (void *)"findme",
        test_item_mapper, NULL);

    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_INT(1, item->id);
    TEST_ASSERT_EQUAL_STRING("findme", item->name);
    TEST_ASSERT_EQUAL_INT(100, item->value);

    test_item_free(item);
}

/* Test query_one_p not found */
void test_query_one_p_not_found(void) {
    TestItem *item = sqlite_dal_query_one_p(test_dal,
        "SELECT id, name, value FROM items WHERE name = ?;",
        bind_name_param, (void *)"nonexistent",
        test_item_mapper, NULL);

    TEST_ASSERT_NULL(item);
}

/* Test query_one_p with failing binder */
void test_query_one_p_binder_fails(void) {
    TestItem *item = sqlite_dal_query_one_p(test_dal,
        "SELECT id, name, value FROM items WHERE name = ?;",
        bind_failing_binder, NULL,
        test_item_mapper, NULL);

    TEST_ASSERT_NULL(item);
}

/* Test exec_p with parameter binding */
void test_exec_p_with_params(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (1, 'delete_me', 10);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (2, 'delete_me', 20);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (3, 'keep', 30);");

    int deleted = sqlite_dal_exec_p(test_dal,
        "DELETE FROM items WHERE name = ?;",
        bind_name_param, (void *)"delete_me");

    TEST_ASSERT_EQUAL_INT(2, deleted);
}

/* Test exec_p with no binder */
void test_exec_p_no_binder(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (id, name, value) VALUES (1, 'a', 10);");

    int updated = sqlite_dal_exec_p(test_dal,
        "UPDATE items SET value = 99;",
        NULL, NULL);

    TEST_ASSERT_EQUAL_INT(1, updated);
}

/* Test exec_p with failing binder */
void test_exec_p_binder_fails(void) {
    int rc = sqlite_dal_exec_p(test_dal,
        "DELETE FROM items WHERE name = ?;",
        bind_failing_binder, NULL);

    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test exists_p found */
void test_exists_p_found(void) {
    sqlite_dal_exec(test_dal, "INSERT INTO items (name, value) VALUES ('findme', 100);");

    int exists = sqlite_dal_exists_p(test_dal,
        "SELECT 1 FROM items WHERE name = ? LIMIT 1;",
        bind_name_param, (void *)"findme");

    TEST_ASSERT_EQUAL_INT(1, exists);
}

/* Test exists_p not found */
void test_exists_p_not_found(void) {
    int exists = sqlite_dal_exists_p(test_dal,
        "SELECT 1 FROM items WHERE name = ? LIMIT 1;",
        bind_name_param, (void *)"nonexistent");

    TEST_ASSERT_EQUAL_INT(0, exists);
}

/* Test exists_p with failing binder */
void test_exists_p_binder_fails(void) {
    int rc = sqlite_dal_exists_p(test_dal,
        "SELECT 1 FROM items WHERE name = ? LIMIT 1;",
        bind_failing_binder, NULL);

    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* Test begin/commit transaction */
void test_transaction_commit(void) {
    int rc = sqlite_dal_begin(test_dal);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sqlite_dal_exec(test_dal, "INSERT INTO items (name, value) VALUES ('txn1', 1);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (name, value) VALUES ('txn2', 2);");

    rc = sqlite_dal_commit(test_dal);
    TEST_ASSERT_EQUAL_INT(0, rc);

    void **items = NULL;
    size_t count = 0;
    sqlite_dal_query_list(test_dal,
        "SELECT id, name, value FROM items;",
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(2, count);

    for (size_t i = 0; i < count; i++) {
        test_item_free(items[i]);
    }
    free(items);
}

/* Test begin/rollback transaction */
void test_transaction_rollback(void) {
    int rc = sqlite_dal_begin(test_dal);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sqlite_dal_exec(test_dal, "INSERT INTO items (name, value) VALUES ('txn1', 1);");
    sqlite_dal_exec(test_dal, "INSERT INTO items (name, value) VALUES ('txn2', 2);");

    rc = sqlite_dal_rollback(test_dal);
    TEST_ASSERT_EQUAL_INT(0, rc);

    void **items = NULL;
    size_t count = 0;
    sqlite_dal_query_list(test_dal,
        "SELECT id, name, value FROM items;",
        test_item_mapper, test_item_free, NULL, &items, &count);

    TEST_ASSERT_EQUAL_INT(0, count);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_destroy);
    RUN_TEST(test_create_null_config);
    RUN_TEST(test_create_default_path);
    RUN_TEST(test_destroy_null);
    RUN_TEST(test_exec_insert);
    RUN_TEST(test_exec_null_dal);
    RUN_TEST(test_exec_null_sql);
    RUN_TEST(test_exec_invalid_sql);
    RUN_TEST(test_exec_int64_delete);
    RUN_TEST(test_exec_int64_null_dal);
    RUN_TEST(test_exists_text_found);
    RUN_TEST(test_exists_text_not_found);
    RUN_TEST(test_exists_text_null_dal);
    RUN_TEST(test_query_list_multiple);
    RUN_TEST(test_query_list_empty);
    RUN_TEST(test_query_list_null_dal);
    RUN_TEST(test_query_one_found);
    RUN_TEST(test_query_one_not_found);
    RUN_TEST(test_query_one_null_dal);
    RUN_TEST(test_lock_unlock_get_db);
    RUN_TEST(test_lock_unlock_null);
    RUN_TEST(test_get_db_null);
    RUN_TEST(test_get_path_null);
    RUN_TEST(test_changes);
    RUN_TEST(test_changes_null);
    RUN_TEST(test_delete_file);
    RUN_TEST(test_delete_file_null);
    RUN_TEST(test_foreign_keys_enabled);
    RUN_TEST(test_schema_initialized);

    /* Parameterized query tests */
    RUN_TEST(test_query_list_p_with_params);
    RUN_TEST(test_query_list_p_no_binder);
    RUN_TEST(test_query_list_p_binder_fails);
    RUN_TEST(test_query_one_p_with_params);
    RUN_TEST(test_query_one_p_not_found);
    RUN_TEST(test_query_one_p_binder_fails);
    RUN_TEST(test_exec_p_with_params);
    RUN_TEST(test_exec_p_no_binder);
    RUN_TEST(test_exec_p_binder_fails);
    RUN_TEST(test_exists_p_found);
    RUN_TEST(test_exists_p_not_found);
    RUN_TEST(test_exists_p_binder_fails);
    RUN_TEST(test_transaction_commit);
    RUN_TEST(test_transaction_rollback);

    return UNITY_END();
}
