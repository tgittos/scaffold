/*
 * message_store.c - Message store implementation using SQLite DAL
 */

#include "message_store.h"
#include "db/sqlite_dal.h"
#include "util/uuid_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static int64_t get_time_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

struct message_store {
    sqlite_dal_t *dal;
};

static const char *SCHEMA_SQL =
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

/* Row mappers */
static void *map_direct_message(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    DirectMessage *msg = calloc(1, sizeof(DirectMessage));
    if (msg == NULL) return NULL;

    const char *id = (const char *)sqlite3_column_text(stmt, 0);
    const char *sender_id = (const char *)sqlite3_column_text(stmt, 1);
    const char *recipient_id = (const char *)sqlite3_column_text(stmt, 2);
    const char *content = (const char *)sqlite3_column_text(stmt, 3);

    if (id) strncpy(msg->id, id, sizeof(msg->id) - 1);
    if (sender_id) strncpy(msg->sender_id, sender_id, sizeof(msg->sender_id) - 1);
    if (recipient_id) strncpy(msg->recipient_id, recipient_id, sizeof(msg->recipient_id) - 1);
    if (content) {
        msg->content = strdup(content);
        if (!msg->content) { free(msg); return NULL; }
    }

    msg->created_at = sqlite3_column_int64(stmt, 4);
    msg->read_at = sqlite3_column_type(stmt, 5) != SQLITE_NULL
                   ? sqlite3_column_int64(stmt, 5) : 0;
    msg->expires_at = sqlite3_column_type(stmt, 6) != SQLITE_NULL
                      ? sqlite3_column_int64(stmt, 6) : 0;
    return msg;
}

static void *map_channel(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    Channel *ch = calloc(1, sizeof(Channel));
    if (ch == NULL) return NULL;

    const char *id = (const char *)sqlite3_column_text(stmt, 0);
    const char *description = (const char *)sqlite3_column_text(stmt, 1);
    const char *creator_id = (const char *)sqlite3_column_text(stmt, 2);

    if (id) strncpy(ch->id, id, sizeof(ch->id) - 1);
    if (description) ch->description = strdup(description);
    if (creator_id) strncpy(ch->creator_id, creator_id, sizeof(ch->creator_id) - 1);

    ch->created_at = sqlite3_column_int64(stmt, 3);
    ch->is_persistent = sqlite3_column_int(stmt, 4);
    return ch;
}

static void *map_channel_message(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    ChannelMessage *msg = calloc(1, sizeof(ChannelMessage));
    if (msg == NULL) return NULL;

    const char *id = (const char *)sqlite3_column_text(stmt, 0);
    const char *channel_id = (const char *)sqlite3_column_text(stmt, 1);
    const char *sender_id = (const char *)sqlite3_column_text(stmt, 2);
    const char *content = (const char *)sqlite3_column_text(stmt, 3);

    if (id) strncpy(msg->id, id, sizeof(msg->id) - 1);
    if (channel_id) strncpy(msg->channel_id, channel_id, sizeof(msg->channel_id) - 1);
    if (sender_id) strncpy(msg->sender_id, sender_id, sizeof(msg->sender_id) - 1);
    if (content) {
        msg->content = strdup(content);
        if (!msg->content) { free(msg); return NULL; }
    }

    msg->created_at = sqlite3_column_int64(stmt, 4);
    return msg;
}

static void *map_string(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    const char *str = (const char *)sqlite3_column_text(stmt, 0);
    return str ? strdup(str) : NULL;
}

/* Additional binder helpers (common ones are in sqlite_dal.h) */
typedef struct { int64_t i1; const char *s1; } BindInt64Text;
typedef struct { int64_t i1; const char *s1; const char *s2; } BindInt64Text2;

static int bind_int64_text(sqlite3_stmt *stmt, void *data) {
    BindInt64Text *b = data;
    sqlite3_bind_int64(stmt, 1, b->i1);
    sqlite3_bind_text(stmt, 2, b->s1, -1, SQLITE_STATIC);
    return 0;
}

static int bind_int64_text2(sqlite3_stmt *stmt, void *data) {
    BindInt64Text2 *b = data;
    sqlite3_bind_int64(stmt, 1, b->i1);
    sqlite3_bind_text(stmt, 2, b->s1, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, b->s2, -1, SQLITE_STATIC);
    return 0;
}

/* Store lifecycle */
message_store_t *message_store_create(const char *db_path) {
    message_store_t *store = calloc(1, sizeof(message_store_t));
    if (store == NULL) return NULL;

    sqlite_dal_config_t config = SQLITE_DAL_CONFIG_DEFAULT;
    config.db_path = db_path;
    config.default_name = "messages.db";
    config.schema_sql = SCHEMA_SQL;

    store->dal = sqlite_dal_create(&config);
    if (store->dal == NULL) {
        free(store);
        return NULL;
    }
    return store;
}

void message_store_destroy(message_store_t *store) {
    if (store == NULL) return;
    sqlite_dal_destroy(store->dal);
    free(store);
}

/* Direct message operations */
int message_send_direct(message_store_t *store, const char *sender_id,
                        const char *recipient_id, const char *content,
                        int ttl_seconds, char *out_id) {
    if (!store || !sender_id || !recipient_id || !content || !out_id) return -1;

    char msg_id[40] = {0};
    if (uuid_generate_v4(msg_id) != 0) return -1;

    int64_t now = get_time_millis();
    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO direct_messages (id, sender_id, recipient_id, content, created_at, expires_at) "
        "VALUES (?, ?, ?, ?, ?, ?);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, msg_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, sender_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, recipient_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, now);
    if (ttl_seconds > 0) sqlite3_bind_int64(stmt, 6, now + ((int64_t)ttl_seconds * 1000));
    else sqlite3_bind_null(stmt, 6);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    if (rc != SQLITE_DONE) return -1;
    strcpy(out_id, msg_id);
    return 0;
}

DirectMessage **message_receive_direct(message_store_t *store, const char *agent_id,
                                       size_t max_count, size_t *out_count) {
    if (!store || !agent_id || !out_count) return NULL;
    *out_count = 0;

    BindTextInt params = { agent_id, max_count > 0 ? (int)max_count : 100 };
    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT id, sender_id, recipient_id, content, created_at, read_at, expires_at "
        "FROM direct_messages WHERE recipient_id = ? AND read_at IS NULL "
        "ORDER BY created_at ASC LIMIT ?;",
        bind_text_int, &params, map_direct_message, (sqlite_item_free_t)direct_message_free,
        NULL, &items, &count);

    if (rc != 0 || count == 0) return NULL;

    /* Mark as read */
    BindInt64Text mark_params = { get_time_millis(), agent_id };
    sqlite_dal_exec_p(store->dal,
        "UPDATE direct_messages SET read_at = ? WHERE recipient_id = ? AND read_at IS NULL;",
        bind_int64_text, &mark_params);

    *out_count = count;
    return (DirectMessage **)items;
}

int message_has_pending(message_store_t *store, const char *agent_id) {
    if (!store || !agent_id) return -1;
    BindText1 params = { agent_id };
    return sqlite_dal_exists_p(store->dal,
        "SELECT 1 FROM direct_messages WHERE recipient_id = ? AND read_at IS NULL LIMIT 1;",
        bind_text1, &params);
}

int channel_has_pending(message_store_t *store, const char *agent_id) {
    if (!store || !agent_id) return -1;
    BindText1 params = { agent_id };
    return sqlite_dal_exists_p(store->dal,
        "SELECT 1 FROM channel_messages cm "
        "JOIN channel_subscriptions cs ON cm.channel_id = cs.channel_id "
        "WHERE cs.agent_id = ? AND cm.created_at > cs.last_read_at LIMIT 1;",
        bind_text1, &params);
}

DirectMessage *message_get_direct(message_store_t *store, const char *message_id) {
    if (!store || !message_id) return NULL;
    BindText1 params = { message_id };
    return sqlite_dal_query_one_p(store->dal,
        "SELECT id, sender_id, recipient_id, content, created_at, read_at, expires_at "
        "FROM direct_messages WHERE id = ?;",
        bind_text1, &params, map_direct_message, NULL);
}

/* Channel operations */
int channel_create(message_store_t *store, const char *channel_name,
                   const char *description, const char *creator_id,
                   int is_persistent) {
    if (!store || !channel_name || !creator_id) return -1;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO channels (id, description, created_by, created_at, is_persistent) "
        "VALUES (?, ?, ?, ?, ?);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);
    if (description) sqlite3_bind_text(stmt, 2, description, -1, SQLITE_STATIC);
    else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, creator_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, get_time_millis());
    sqlite3_bind_int(stmt, 5, is_persistent ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

Channel *channel_get(message_store_t *store, const char *channel_name) {
    if (!store || !channel_name) return NULL;
    BindText1 params = { channel_name };
    return sqlite_dal_query_one_p(store->dal,
        "SELECT id, description, created_by, created_at, is_persistent FROM channels WHERE id = ?;",
        bind_text1, &params, map_channel, NULL);
}

Channel **channel_list(message_store_t *store, size_t *out_count) {
    if (!store || !out_count) return NULL;
    *out_count = 0;

    void **items = NULL;
    size_t count = 0;
    int rc = sqlite_dal_query_list(store->dal,
        "SELECT id, description, created_by, created_at, is_persistent FROM channels ORDER BY created_at;",
        map_channel, (sqlite_item_free_t)channel_free, NULL, &items, &count);

    if (rc != 0 || count == 0) return NULL;
    *out_count = count;
    return (Channel **)items;
}

int channel_delete(message_store_t *store, const char *channel_name) {
    if (!store || !channel_name) return -1;
    BindText1 params = { channel_name };
    int deleted = sqlite_dal_exec_p(store->dal, "DELETE FROM channels WHERE id = ?;",
                                     bind_text1, &params);
    return (deleted > 0) ? 0 : -1;
}

int channel_subscribe(message_store_t *store, const char *channel_name, const char *agent_id) {
    if (!store || !channel_name || !agent_id) return -1;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO channel_subscriptions (channel_id, agent_id, subscribed_at, last_read_at) "
        "VALUES (?, ?, ?, 0);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, get_time_millis());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int channel_unsubscribe(message_store_t *store, const char *channel_name, const char *agent_id) {
    if (!store || !channel_name || !agent_id) return -1;
    BindText2 params = { channel_name, agent_id };
    int changes = sqlite_dal_exec_p(store->dal,
        "DELETE FROM channel_subscriptions WHERE channel_id = ? AND agent_id = ?;",
        bind_text2, &params);
    return (changes >= 0) ? 0 : -1;
}

int channel_is_subscribed(message_store_t *store, const char *channel_name, const char *agent_id) {
    if (!store || !channel_name || !agent_id) return -1;
    BindText2 params = { channel_name, agent_id };
    return sqlite_dal_exists_p(store->dal,
        "SELECT 1 FROM channel_subscriptions WHERE channel_id = ? AND agent_id = ? LIMIT 1;",
        bind_text2, &params);
}

char **channel_get_subscribers(message_store_t *store, const char *channel_name, size_t *out_count) {
    if (!store || !channel_name || !out_count) return NULL;
    *out_count = 0;

    BindText1 params = { channel_name };
    void **items = NULL;
    size_t count = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT agent_id FROM channel_subscriptions WHERE channel_id = ?;",
        bind_text1, &params, map_string, free, NULL, &items, &count);

    if (rc != 0 || count == 0) return NULL;
    *out_count = count;
    return (char **)items;
}

char **channel_get_agent_subscriptions(message_store_t *store, const char *agent_id, size_t *out_count) {
    if (!store || !agent_id || !out_count) return NULL;
    *out_count = 0;

    BindText1 params = { agent_id };
    void **items = NULL;
    size_t count = 0;
    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT channel_id FROM channel_subscriptions WHERE agent_id = ?;",
        bind_text1, &params, map_string, free, NULL, &items, &count);

    if (rc != 0 || count == 0) return NULL;
    *out_count = count;
    return (char **)items;
}

int channel_publish(message_store_t *store, const char *channel_name,
                    const char *sender_id, const char *content, char *out_id) {
    if (!store || !channel_name || !sender_id || !content || !out_id) return -1;

    char msg_id[40] = {0};
    if (uuid_generate_v4(msg_id) != 0) return -1;

    sqlite_dal_lock(store->dal);
    sqlite3 *db = sqlite_dal_get_db(store->dal);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO channel_messages (id, channel_id, sender_id, content, created_at) "
        "VALUES (?, ?, ?, ?, ?);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { sqlite_dal_unlock(store->dal); return -1; }

    sqlite3_bind_text(stmt, 1, msg_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, sender_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, get_time_millis());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite_dal_unlock(store->dal);

    if (rc != SQLITE_DONE) return -1;
    strcpy(out_id, msg_id);
    return 0;
}

typedef struct { const char *agent_id; const char *channel_name; int limit; } ReceiveParams;

static int bind_receive_params(sqlite3_stmt *stmt, void *data) {
    ReceiveParams *p = data;
    sqlite3_bind_text(stmt, 1, p->agent_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, p->channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, p->limit);
    return 0;
}

ChannelMessage **channel_receive(message_store_t *store, const char *channel_name,
                                 const char *agent_id, size_t max_count, size_t *out_count) {
    if (!store || !channel_name || !agent_id || !out_count) return NULL;
    *out_count = 0;

    ReceiveParams params = { agent_id, channel_name, max_count > 0 ? (int)max_count : 100 };
    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT cm.id, cm.channel_id, cm.sender_id, cm.content, cm.created_at "
        "FROM channel_messages cm "
        "JOIN channel_subscriptions cs ON cm.channel_id = cs.channel_id "
        "WHERE cs.agent_id = ? AND cm.channel_id = ? AND cm.created_at > cs.last_read_at "
        "ORDER BY cm.created_at ASC LIMIT ?;",
        bind_receive_params, &params, map_channel_message, (sqlite_item_free_t)channel_message_free,
        NULL, &items, &count);

    if (rc != 0 || count == 0) return NULL;

    /* Set last_read_at to the latest message's timestamp, not wall clock.
     * Using wall clock could miss messages published in the same millisecond. */
    ChannelMessage *last = (ChannelMessage *)items[count - 1];
    BindInt64Text2 mark_params = { last->created_at, channel_name, agent_id };
    sqlite_dal_exec_p(store->dal,
        "UPDATE channel_subscriptions SET last_read_at = ? WHERE channel_id = ? AND agent_id = ?;",
        bind_int64_text2, &mark_params);

    *out_count = count;
    return (ChannelMessage **)items;
}

ChannelMessage **channel_receive_all(message_store_t *store, const char *agent_id,
                                     size_t max_count, size_t *out_count) {
    if (!store || !agent_id || !out_count) return NULL;
    *out_count = 0;

    BindTextInt params = { agent_id, max_count > 0 ? (int)max_count : 100 };
    void **items = NULL;
    size_t count = 0;

    int rc = sqlite_dal_query_list_p(store->dal,
        "SELECT cm.id, cm.channel_id, cm.sender_id, cm.content, cm.created_at "
        "FROM channel_messages cm "
        "JOIN channel_subscriptions cs ON cm.channel_id = cs.channel_id "
        "WHERE cs.agent_id = ? AND cm.created_at > cs.last_read_at "
        "ORDER BY cm.created_at ASC LIMIT ?;",
        bind_text_int, &params, map_channel_message, (sqlite_item_free_t)channel_message_free,
        NULL, &items, &count);

    if (rc != 0 || count == 0) return NULL;

    /* Set last_read_at to the latest message's timestamp, not wall clock. */
    ChannelMessage *last = (ChannelMessage *)items[count - 1];
    BindInt64Text mark_params = { last->created_at, agent_id };
    sqlite_dal_exec_p(store->dal,
        "UPDATE channel_subscriptions SET last_read_at = ? WHERE agent_id = ?;",
        bind_int64_text, &mark_params);

    *out_count = count;
    return (ChannelMessage **)items;
}

/* Cleanup operations */
int message_cleanup_read(message_store_t *store, int grace_period_seconds) {
    if (!store) return -1;
    BindInt64 params = { get_time_millis() - ((int64_t)grace_period_seconds * 1000) };
    return sqlite_dal_exec_p(store->dal,
        "DELETE FROM direct_messages WHERE read_at IS NOT NULL AND read_at < ?;",
        bind_int64, &params);
}

int message_cleanup_expired(message_store_t *store) {
    if (!store) return -1;
    BindInt64 params = { get_time_millis() };
    return sqlite_dal_exec_p(store->dal,
        "DELETE FROM direct_messages WHERE expires_at IS NOT NULL AND expires_at < ?;",
        bind_int64, &params);
}

int message_cleanup_agent(message_store_t *store, const char *agent_id) {
    if (!store || !agent_id) return -1;

    /* Hold lock for entire transaction */
    sqlite_dal_lock(store->dal);

    if (sqlite_dal_begin_unlocked(store->dal) != 0) {
        sqlite_dal_unlock(store->dal);
        return -1;
    }

    sqlite3 *db = sqlite_dal_get_db(store->dal);

    /* Delete direct messages */
    sqlite3_stmt *dm_stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "DELETE FROM direct_messages WHERE sender_id = ? OR recipient_id = ?;",
        -1, &dm_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(dm_stmt, 1, agent_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(dm_stmt, 2, agent_id, -1, SQLITE_STATIC);
        sqlite3_step(dm_stmt);
        sqlite3_finalize(dm_stmt);
    }

    /* Delete subscriptions */
    sqlite3_stmt *sub_stmt = NULL;
    rc = sqlite3_prepare_v2(db,
        "DELETE FROM channel_subscriptions WHERE agent_id = ?;",
        -1, &sub_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(sub_stmt, 1, agent_id, -1, SQLITE_STATIC);
        sqlite3_step(sub_stmt);
        sqlite3_finalize(sub_stmt);
    }

    int result = sqlite_dal_commit_unlocked(store->dal);
    sqlite_dal_unlock(store->dal);
    return result;
}

int message_cleanup_channel_messages(message_store_t *store, int max_age_seconds) {
    if (!store) return -1;
    BindInt64 params = { get_time_millis() - ((int64_t)max_age_seconds * 1000) };
    return sqlite_dal_exec_p(store->dal,
        "DELETE FROM channel_messages WHERE channel_id IN "
        "(SELECT id FROM channels WHERE is_persistent = 0) AND created_at < ?;",
        bind_int64, &params);
}

/* Free functions */
void direct_message_free(DirectMessage *msg) {
    if (msg) { free(msg->content); free(msg); }
}

void direct_message_free_list(DirectMessage **msgs, size_t count) {
    if (!msgs) return;
    for (size_t i = 0; i < count; i++) direct_message_free(msgs[i]);
    free(msgs);
}

void channel_free(Channel *channel) {
    if (channel) { free(channel->description); free(channel); }
}

void channel_free_list(Channel **channels, size_t count) {
    if (!channels) return;
    for (size_t i = 0; i < count; i++) channel_free(channels[i]);
    free(channels);
}

void subscription_free(Subscription *sub) { free(sub); }

void subscription_free_list(Subscription **subs, size_t count) {
    if (!subs) return;
    for (size_t i = 0; i < count; i++) subscription_free(subs[i]);
    free(subs);
}

void channel_message_free(ChannelMessage *msg) {
    if (msg) { free(msg->content); free(msg); }
}

void channel_message_free_list(ChannelMessage **msgs, size_t count) {
    if (!msgs) return;
    for (size_t i = 0; i < count; i++) channel_message_free(msgs[i]);
    free(msgs);
}

void channel_subscribers_free(char **subscribers, size_t count) {
    if (!subscribers) return;
    for (size_t i = 0; i < count; i++) free(subscribers[i]);
    free(subscribers);
}

void channel_subscriptions_free(char **subscriptions, size_t count) {
    if (!subscriptions) return;
    for (size_t i = 0; i < count; i++) free(subscriptions[i]);
    free(subscriptions);
}
