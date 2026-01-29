#include "message_store.h"
#include "../utils/uuid_utils.h"
#include "../utils/ptrarray.h"
#include "../utils/ralph_home.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int64_t get_time_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

PTRARRAY_DEFINE(DirectMessageArray, DirectMessage)
PTRARRAY_DEFINE(ChannelArray, Channel)
PTRARRAY_DEFINE(SubscriptionArray, Subscription)
PTRARRAY_DEFINE(ChannelMessageArray, ChannelMessage)

struct message_store {
    sqlite3* db;
    pthread_mutex_t mutex;
    char* db_path;
};

static message_store_t* g_store_instance = NULL;
static pthread_once_t g_store_once = PTHREAD_ONCE_INIT;

static const char* SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS direct_messages ("
    "    id TEXT PRIMARY KEY,"
    "    sender_id TEXT NOT NULL,"
    "    recipient_id TEXT NOT NULL,"
    "    content TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    read_at INTEGER DEFAULT NULL,"
    "    expires_at INTEGER DEFAULT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS channels ("
    "    id TEXT PRIMARY KEY,"
    "    description TEXT,"
    "    created_by TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    is_persistent INTEGER DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS channel_subscriptions ("
    "    channel_id TEXT NOT NULL,"
    "    agent_id TEXT NOT NULL,"
    "    subscribed_at INTEGER NOT NULL,"
    "    last_read_at INTEGER DEFAULT 0,"
    "    PRIMARY KEY (channel_id, agent_id),"
    "    FOREIGN KEY (channel_id) REFERENCES channels(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS channel_messages ("
    "    id TEXT PRIMARY KEY,"
    "    channel_id TEXT NOT NULL,"
    "    sender_id TEXT NOT NULL,"
    "    content TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    FOREIGN KEY (channel_id) REFERENCES channels(id) ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_dm_recipient ON direct_messages(recipient_id, read_at);"
    "CREATE INDEX IF NOT EXISTS idx_dm_expires ON direct_messages(expires_at) WHERE expires_at IS NOT NULL;"
    "CREATE INDEX IF NOT EXISTS idx_cm_channel ON channel_messages(channel_id, created_at);"
    "CREATE INDEX IF NOT EXISTS idx_subs_agent ON channel_subscriptions(agent_id);";

static int init_schema(sqlite3* db);
static char* get_default_db_path(void);

message_store_t* message_store_create(const char* db_path) {
    message_store_t* store = calloc(1, sizeof(message_store_t));
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

    if (pthread_mutex_init(&store->mutex, NULL) != 0) {
        free(store->db_path);
        free(store);
        return NULL;
    }

    int rc = sqlite3_open(store->db_path, &store->db);
    if (rc != SQLITE_OK) {
        pthread_mutex_destroy(&store->mutex);
        free(store->db_path);
        free(store);
        return NULL;
    }

    sqlite3_exec(store->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
    sqlite3_exec(store->db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
    sqlite3_busy_timeout(store->db, 5000);

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
    g_store_instance = message_store_create(NULL);
}

message_store_t* message_store_get_instance(void) {
    pthread_once(&g_store_once, create_store_instance);
    return g_store_instance;
}

void message_store_destroy(message_store_t* store) {
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

void message_store_reset_instance(void) {
    if (g_store_instance != NULL) {
        message_store_destroy(g_store_instance);
        g_store_instance = NULL;
    }
    g_store_once = (pthread_once_t)PTHREAD_ONCE_INIT;

    char* db_path = ralph_home_path("messages.db");
    if (db_path != NULL) {
        unlink(db_path);
        free(db_path);
    }
}

static char* get_default_db_path(void) {
    if (ralph_home_ensure_exists() != 0) {
        return NULL;
    }
    return ralph_home_path("messages.db");
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

static DirectMessage* row_to_direct_message(sqlite3_stmt* stmt) {
    DirectMessage* msg = calloc(1, sizeof(DirectMessage));
    if (msg == NULL) {
        return NULL;
    }

    const char* id = (const char*)sqlite3_column_text(stmt, 0);
    const char* sender_id = (const char*)sqlite3_column_text(stmt, 1);
    const char* recipient_id = (const char*)sqlite3_column_text(stmt, 2);
    const char* content = (const char*)sqlite3_column_text(stmt, 3);

    if (id != NULL) {
        strncpy(msg->id, id, sizeof(msg->id) - 1);
    }
    if (sender_id != NULL) {
        strncpy(msg->sender_id, sender_id, sizeof(msg->sender_id) - 1);
    }
    if (recipient_id != NULL) {
        strncpy(msg->recipient_id, recipient_id, sizeof(msg->recipient_id) - 1);
    }
    if (content != NULL) {
        msg->content = strdup(content);
        if (msg->content == NULL) {
            free(msg);
            return NULL;
        }
    }

    msg->created_at = (time_t)sqlite3_column_int64(stmt, 4);
    msg->read_at = sqlite3_column_type(stmt, 5) != SQLITE_NULL
                       ? (time_t)sqlite3_column_int64(stmt, 5)
                       : 0;
    msg->expires_at = sqlite3_column_type(stmt, 6) != SQLITE_NULL
                          ? (time_t)sqlite3_column_int64(stmt, 6)
                          : 0;

    return msg;
}

static Channel* row_to_channel(sqlite3_stmt* stmt) {
    Channel* ch = calloc(1, sizeof(Channel));
    if (ch == NULL) {
        return NULL;
    }

    const char* id = (const char*)sqlite3_column_text(stmt, 0);
    const char* description = (const char*)sqlite3_column_text(stmt, 1);
    const char* creator_id = (const char*)sqlite3_column_text(stmt, 2);

    if (id != NULL) {
        strncpy(ch->id, id, sizeof(ch->id) - 1);
    }
    if (description != NULL) {
        ch->description = strdup(description);
    }
    if (creator_id != NULL) {
        strncpy(ch->creator_id, creator_id, sizeof(ch->creator_id) - 1);
    }

    ch->created_at = (time_t)sqlite3_column_int64(stmt, 3);
    ch->is_persistent = sqlite3_column_int(stmt, 4);

    return ch;
}

static ChannelMessage* row_to_channel_message(sqlite3_stmt* stmt) {
    ChannelMessage* msg = calloc(1, sizeof(ChannelMessage));
    if (msg == NULL) {
        return NULL;
    }

    const char* id = (const char*)sqlite3_column_text(stmt, 0);
    const char* channel_id = (const char*)sqlite3_column_text(stmt, 1);
    const char* sender_id = (const char*)sqlite3_column_text(stmt, 2);
    const char* content = (const char*)sqlite3_column_text(stmt, 3);

    if (id != NULL) {
        strncpy(msg->id, id, sizeof(msg->id) - 1);
    }
    if (channel_id != NULL) {
        strncpy(msg->channel_id, channel_id, sizeof(msg->channel_id) - 1);
    }
    if (sender_id != NULL) {
        strncpy(msg->sender_id, sender_id, sizeof(msg->sender_id) - 1);
    }
    if (content != NULL) {
        msg->content = strdup(content);
        if (msg->content == NULL) {
            free(msg);
            return NULL;
        }
    }

    msg->created_at = (time_t)sqlite3_column_int64(stmt, 4);

    return msg;
}

int message_send_direct(message_store_t* store, const char* sender_id,
                        const char* recipient_id, const char* content,
                        int ttl_seconds, char* out_id) {
    if (store == NULL || sender_id == NULL || recipient_id == NULL ||
        content == NULL || out_id == NULL) {
        return -1;
    }

    char msg_id[40] = {0};
    if (uuid_generate_v4(msg_id) != 0) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "INSERT INTO direct_messages (id, sender_id, recipient_id, content, created_at, expires_at) "
        "VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    int64_t now_millis = get_time_millis();

    sqlite3_bind_text(stmt, 1, msg_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, sender_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, recipient_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, now_millis);
    if (ttl_seconds > 0) {
        sqlite3_bind_int64(stmt, 6, now_millis + ((int64_t)ttl_seconds * 1000));
    } else {
        sqlite3_bind_null(stmt, 6);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (rc != SQLITE_DONE) {
        return -1;
    }

    strcpy(out_id, msg_id);
    return 0;
}

DirectMessage** message_receive_direct(message_store_t* store, const char* agent_id,
                                       size_t max_count, size_t* out_count) {
    if (store == NULL || agent_id == NULL || out_count == NULL) {
        return NULL;
    }

    *out_count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT id, sender_id, recipient_id, content, created_at, read_at, expires_at "
        "FROM direct_messages "
        "WHERE recipient_id = ? AND read_at IS NULL "
        "ORDER BY created_at ASC LIMIT ?;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_count > 0 ? (int)max_count : 100);

    DirectMessageArray arr;
    if (DirectMessageArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DirectMessage* msg = row_to_direct_message(stmt);
        if (msg != NULL) {
            if (DirectMessageArray_push(&arr, msg) != 0) {
                direct_message_free(msg);
                break;
            }
        }
    }

    sqlite3_finalize(stmt);

    if (arr.count > 0) {
        int64_t now_millis = get_time_millis();
        const char* update_sql =
            "UPDATE direct_messages SET read_at = ? WHERE recipient_id = ? AND read_at IS NULL;";
        sqlite3_stmt* update_stmt = NULL;
        if (sqlite3_prepare_v2(store->db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(update_stmt, 1, now_millis);
            sqlite3_bind_text(update_stmt, 2, agent_id, -1, SQLITE_STATIC);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        }
    }

    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        DirectMessageArray_destroy_shallow(&arr);
        return NULL;
    }

    *out_count = arr.count;
    return DirectMessageArray_steal(&arr, NULL);
}

int message_has_pending(message_store_t* store, const char* agent_id) {
    if (store == NULL || agent_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT 1 FROM direct_messages WHERE recipient_id = ? AND read_at IS NULL LIMIT 1;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);

    int has_pending = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return has_pending;
}

DirectMessage* message_get_direct(message_store_t* store, const char* message_id) {
    if (store == NULL || message_id == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT id, sender_id, recipient_id, content, created_at, read_at, expires_at "
        "FROM direct_messages WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, message_id, -1, SQLITE_STATIC);

    DirectMessage* msg = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        msg = row_to_direct_message(stmt);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return msg;
}

int channel_create(message_store_t* store, const char* channel_name,
                   const char* description, const char* creator_id,
                   int is_persistent) {
    if (store == NULL || channel_name == NULL || creator_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "INSERT INTO channels (id, description, created_by, created_at, is_persistent) "
        "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);
    if (description != NULL) {
        sqlite3_bind_text(stmt, 2, description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_text(stmt, 3, creator_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, get_time_millis());
    sqlite3_bind_int(stmt, 5, is_persistent ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

Channel* channel_get(message_store_t* store, const char* channel_name) {
    if (store == NULL || channel_name == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT id, description, created_by, created_at, is_persistent "
        "FROM channels WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);

    Channel* ch = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ch = row_to_channel(stmt);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return ch;
}

Channel** channel_list(message_store_t* store, size_t* out_count) {
    if (store == NULL || out_count == NULL) {
        return NULL;
    }

    *out_count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT id, description, created_by, created_at, is_persistent "
        "FROM channels ORDER BY created_at;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    ChannelArray arr;
    if (ChannelArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Channel* ch = row_to_channel(stmt);
        if (ch != NULL) {
            if (ChannelArray_push(&arr, ch) != 0) {
                channel_free(ch);
                break;
            }
        }
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        ChannelArray_destroy_shallow(&arr);
        return NULL;
    }

    *out_count = arr.count;
    return ChannelArray_steal(&arr, NULL);
}

int channel_delete(message_store_t* store, const char* channel_name) {
    if (store == NULL || channel_name == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql = "DELETE FROM channels WHERE id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int channel_subscribe(message_store_t* store, const char* channel_name,
                      const char* agent_id) {
    if (store == NULL || channel_name == NULL || agent_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "INSERT OR REPLACE INTO channel_subscriptions (channel_id, agent_id, subscribed_at, last_read_at) "
        "VALUES (?, ?, ?, 0);";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, get_time_millis());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int channel_unsubscribe(message_store_t* store, const char* channel_name,
                        const char* agent_id) {
    if (store == NULL || channel_name == NULL || agent_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "DELETE FROM channel_subscriptions WHERE channel_id = ? AND agent_id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int channel_is_subscribed(message_store_t* store, const char* channel_name,
                          const char* agent_id) {
    if (store == NULL || channel_name == NULL || agent_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT 1 FROM channel_subscriptions WHERE channel_id = ? AND agent_id = ? LIMIT 1;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);

    int subscribed = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return subscribed;
}

char** channel_get_subscribers(message_store_t* store, const char* channel_name,
                               size_t* out_count) {
    if (store == NULL || channel_name == NULL || out_count == NULL) {
        return NULL;
    }

    *out_count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT agent_id FROM channel_subscriptions WHERE channel_id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);

    StringArray arr;
    if (StringArray_init(&arr, free) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* agent_id = (const char*)sqlite3_column_text(stmt, 0);
        if (agent_id != NULL) {
            char* copy = strdup(agent_id);
            if (copy != NULL) {
                if (StringArray_push(&arr, copy) != 0) {
                    free(copy);
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

    *out_count = arr.count;
    char** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;
    return result;
}

char** channel_get_agent_subscriptions(message_store_t* store, const char* agent_id,
                                       size_t* out_count) {
    if (store == NULL || agent_id == NULL || out_count == NULL) {
        return NULL;
    }

    *out_count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT channel_id FROM channel_subscriptions WHERE agent_id = ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);

    StringArray arr;
    if (StringArray_init(&arr, free) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* channel_id = (const char*)sqlite3_column_text(stmt, 0);
        if (channel_id != NULL) {
            char* copy = strdup(channel_id);
            if (copy != NULL) {
                if (StringArray_push(&arr, copy) != 0) {
                    free(copy);
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

    *out_count = arr.count;
    char** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;
    return result;
}

int channel_publish(message_store_t* store, const char* channel_name,
                    const char* sender_id, const char* content, char* out_id) {
    if (store == NULL || channel_name == NULL || sender_id == NULL ||
        content == NULL || out_id == NULL) {
        return -1;
    }

    char msg_id[40] = {0};
    if (uuid_generate_v4(msg_id) != 0) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "INSERT INTO channel_messages (id, channel_id, sender_id, content, created_at) "
        "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, msg_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, sender_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, get_time_millis());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    if (rc != SQLITE_DONE) {
        return -1;
    }

    strcpy(out_id, msg_id);
    return 0;
}


ChannelMessage** channel_receive(message_store_t* store, const char* channel_name,
                                 const char* agent_id, size_t max_count,
                                 size_t* out_count) {
    if (store == NULL || channel_name == NULL || agent_id == NULL || out_count == NULL) {
        return NULL;
    }

    *out_count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT cm.id, cm.channel_id, cm.sender_id, cm.content, cm.created_at "
        "FROM channel_messages cm "
        "JOIN channel_subscriptions cs ON cm.channel_id = cs.channel_id "
        "WHERE cs.agent_id = ? AND cm.channel_id = ? AND cm.created_at > cs.last_read_at "
        "ORDER BY cm.created_at ASC LIMIT ?;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, max_count > 0 ? (int)max_count : 100);

    ChannelMessageArray arr;
    if (ChannelMessageArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChannelMessage* msg = row_to_channel_message(stmt);
        if (msg != NULL) {
            if (ChannelMessageArray_push(&arr, msg) != 0) {
                channel_message_free(msg);
                break;
            }
        }
    }

    sqlite3_finalize(stmt);

    if (arr.count > 0) {
        int64_t now_millis = get_time_millis();
        const char* update_sql =
            "UPDATE channel_subscriptions SET last_read_at = ? "
            "WHERE channel_id = ? AND agent_id = ?;";
        sqlite3_stmt* update_stmt = NULL;
        if (sqlite3_prepare_v2(store->db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(update_stmt, 1, now_millis);
            sqlite3_bind_text(update_stmt, 2, channel_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(update_stmt, 3, agent_id, -1, SQLITE_STATIC);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        }
    }

    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        ChannelMessageArray_destroy_shallow(&arr);
        return NULL;
    }

    *out_count = arr.count;
    return ChannelMessageArray_steal(&arr, NULL);
}

ChannelMessage** channel_receive_all(message_store_t* store, const char* agent_id,
                                     size_t max_count, size_t* out_count) {
    if (store == NULL || agent_id == NULL || out_count == NULL) {
        return NULL;
    }

    *out_count = 0;
    pthread_mutex_lock(&store->mutex);

    const char* sql =
        "SELECT cm.id, cm.channel_id, cm.sender_id, cm.content, cm.created_at "
        "FROM channel_messages cm "
        "JOIN channel_subscriptions cs ON cm.channel_id = cs.channel_id "
        "WHERE cs.agent_id = ? AND cm.created_at > cs.last_read_at "
        "ORDER BY cm.created_at ASC LIMIT ?;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_count > 0 ? (int)max_count : 100);

    ChannelMessageArray arr;
    if (ChannelMessageArray_init(&arr, NULL) != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&store->mutex);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChannelMessage* msg = row_to_channel_message(stmt);
        if (msg != NULL) {
            if (ChannelMessageArray_push(&arr, msg) != 0) {
                channel_message_free(msg);
                break;
            }
        }
    }

    sqlite3_finalize(stmt);

    if (arr.count > 0) {
        int64_t now_millis = get_time_millis();
        const char* update_sql =
            "UPDATE channel_subscriptions SET last_read_at = ? WHERE agent_id = ?;";
        sqlite3_stmt* update_stmt = NULL;
        if (sqlite3_prepare_v2(store->db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(update_stmt, 1, now_millis);
            sqlite3_bind_text(update_stmt, 2, agent_id, -1, SQLITE_STATIC);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        }
    }

    pthread_mutex_unlock(&store->mutex);

    if (arr.count == 0) {
        ChannelMessageArray_destroy_shallow(&arr);
        return NULL;
    }

    *out_count = arr.count;
    return ChannelMessageArray_steal(&arr, NULL);
}

int message_cleanup_read(message_store_t* store, int grace_period_seconds) {
    if (store == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    int64_t cutoff_millis = get_time_millis() - ((int64_t)grace_period_seconds * 1000);
    const char* sql =
        "DELETE FROM direct_messages WHERE read_at IS NOT NULL AND read_at < ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, cutoff_millis);
    rc = sqlite3_step(stmt);
    int deleted = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? deleted : -1;
}

int message_cleanup_expired(message_store_t* store) {
    if (store == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    int64_t now_millis = get_time_millis();
    const char* sql =
        "DELETE FROM direct_messages WHERE expires_at IS NOT NULL AND expires_at < ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, now_millis);
    rc = sqlite3_step(stmt);
    int deleted = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? deleted : -1;
}

int message_cleanup_agent(message_store_t* store, const char* agent_id) {
    if (store == NULL || agent_id == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    int rc = sqlite3_exec(store->db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    const char* del_dm_sql =
        "DELETE FROM direct_messages WHERE sender_id = ? OR recipient_id = ?;";
    sqlite3_stmt* stmt = NULL;
    rc = sqlite3_prepare_v2(store->db, del_dm_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const char* del_subs_sql =
        "DELETE FROM channel_subscriptions WHERE agent_id = ?;";
    rc = sqlite3_prepare_v2(store->db, del_subs_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    rc = sqlite3_exec(store->db, "COMMIT;", NULL, NULL, NULL);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_OK) ? 0 : -1;
}

int message_cleanup_channel_messages(message_store_t* store, int max_age_seconds) {
    if (store == NULL) {
        return -1;
    }

    pthread_mutex_lock(&store->mutex);

    int64_t cutoff_millis = get_time_millis() - ((int64_t)max_age_seconds * 1000);
    const char* sql =
        "DELETE FROM channel_messages WHERE channel_id IN "
        "(SELECT id FROM channels WHERE is_persistent = 0) AND created_at < ?;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&store->mutex);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, cutoff_millis);
    rc = sqlite3_step(stmt);
    int deleted = sqlite3_changes(store->db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&store->mutex);

    return (rc == SQLITE_DONE) ? deleted : -1;
}

void direct_message_free(DirectMessage* msg) {
    if (msg == NULL) {
        return;
    }
    free(msg->content);
    free(msg);
}

void direct_message_free_list(DirectMessage** msgs, size_t count) {
    if (msgs == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        direct_message_free(msgs[i]);
    }
    free(msgs);
}

void channel_free(Channel* channel) {
    if (channel == NULL) {
        return;
    }
    free(channel->description);
    free(channel);
}

void channel_free_list(Channel** channels, size_t count) {
    if (channels == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        channel_free(channels[i]);
    }
    free(channels);
}

void subscription_free(Subscription* sub) {
    if (sub == NULL) {
        return;
    }
    free(sub);
}

void subscription_free_list(Subscription** subs, size_t count) {
    if (subs == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        subscription_free(subs[i]);
    }
    free(subs);
}

void channel_message_free(ChannelMessage* msg) {
    if (msg == NULL) {
        return;
    }
    free(msg->content);
    free(msg);
}

void channel_message_free_list(ChannelMessage** msgs, size_t count) {
    if (msgs == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        channel_message_free(msgs[i]);
    }
    free(msgs);
}
