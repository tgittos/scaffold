#include "goal_progress.h"
#include "../util/ansi_codes.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Diamond marker prefixing every rendered progress line: "<marker> <goal>: <symbol> <detail>" */
#define GOAL_PROGRESS_MARKER "\xE2\x97\x86" /* U+25C6 BLACK DIAMOND */

char *goal_progress_channel_name(const char *project) {
    const char *p = (project != NULL && project[0] != '\0') ? project : "default";
    size_t len = strlen("goals:") + strlen(p) + 1;
    char *buf = malloc(len);
    if (buf == NULL) return NULL;
    snprintf(buf, len, "goals:%s", p);
    return buf;
}

/* Channels must exist before channel_subscribe()/channel_publish() can
 * reference them (channel_subscriptions/channel_messages both carry a FK
 * to channels(id)). Failure here almost always means the channel already
 * exists, which is fine — ignore it. */
static void ensure_channel_exists(message_store_t *store, const char *channel_name) {
    channel_create(store, channel_name, "Goal progress events", "system", 1);
}

int goal_progress_publish(message_store_t *store, const char *project,
                           const char *event, const char *goal_name,
                           const char *detail) {
    if (store == NULL || event == NULL || goal_name == NULL) return -1;

    char *channel = goal_progress_channel_name(project);
    if (channel == NULL) return -1;

    ensure_channel_exists(store, channel);

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) { free(channel); return -1; }
    cJSON_AddStringToObject(json, "event", event);
    cJSON_AddStringToObject(json, "goal_name", goal_name);
    cJSON_AddStringToObject(json, "detail", detail != NULL ? detail : "");

    char *content = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (content == NULL) { free(channel); return -1; }

    char msg_id[40];
    int rc = channel_publish(store, channel, "supervisor", content, msg_id);

    free(content);
    free(channel);
    return rc;
}

int goal_progress_subscribe(message_store_t *store, const char *project,
                             const char *subscriber_id) {
    if (store == NULL || subscriber_id == NULL) return -1;

    char *channel = goal_progress_channel_name(project);
    if (channel == NULL) return -1;

    ensure_channel_exists(store, channel);
    int rc = channel_subscribe(store, channel, subscriber_id);
    free(channel);
    return rc;
}

/* Display symbol for each event tag. Unrecognised/NULL events fall back to
 * a plain bullet so new event types degrade gracefully instead of crashing
 * the formatter. */
static const char *event_symbol(const char *event) {
    if (event == NULL) return TERM_SYM_BULLET;
    if (strcmp(event, "action_finished") == 0) return TERM_SYM_SUCCESS;
    if (strcmp(event, "goal_completed") == 0) return TERM_SYM_SUCCESS;
    if (strcmp(event, "action_failed") == 0) return TERM_SYM_ERROR;
    if (strcmp(event, "goal_failed") == 0) return TERM_SYM_ERROR;
    if (strcmp(event, "action_started") == 0) return TERM_SYM_ACTIVE;
    if (strcmp(event, "phase_started") == 0) return TERM_SYM_ACTIVE;
    return TERM_SYM_BULLET;
}

char *goal_progress_format(const char *json_event) {
    if (json_event == NULL) return NULL;

    cJSON *json = cJSON_Parse(json_event);
    if (json == NULL) return NULL;

    cJSON *event_item = cJSON_GetObjectItem(json, "event");
    cJSON *name_item = cJSON_GetObjectItem(json, "goal_name");
    cJSON *detail_item = cJSON_GetObjectItem(json, "detail");

    const char *event = cJSON_IsString(event_item) ? event_item->valuestring : NULL;
    const char *goal_name = cJSON_IsString(name_item) ? name_item->valuestring : NULL;
    const char *detail = cJSON_IsString(detail_item) ? detail_item->valuestring : "";

    if (goal_name == NULL) {
        cJSON_Delete(json);
        return NULL;
    }

    const char *symbol = event_symbol(event);

    size_t len = strlen(GOAL_PROGRESS_MARKER) + strlen(goal_name) + strlen(symbol)
                 + strlen(detail) + 16;
    char *line = malloc(len);
    if (line != NULL) {
        snprintf(line, len, GOAL_PROGRESS_MARKER " %s: %s %s", goal_name, symbol, detail);
    }

    cJSON_Delete(json);
    return line;
}

char *goal_progress_quit_warning(const char **names, size_t count) {
    if (names == NULL || count == 0) return NULL;

    size_t joined_len = 0;
    for (size_t i = 0; i < count; i++) {
        joined_len += strlen(names[i]) + 2; /* ", " separator budget */
    }

    char *joined = malloc(joined_len + 1);
    if (joined == NULL) return NULL;
    joined[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        if (i > 0) strcat(joined, ", ");
        strcat(joined, names[i]);
    }

    const char *noun = (count == 1) ? "goal" : "goals";
    size_t buf_size = strlen(joined) + 128;
    char *msg = malloc(buf_size);
    if (msg == NULL) { free(joined); return NULL; }

    snprintf(msg, buf_size,
             "%zu %s still running in the background (%s). "
             "Run scaffold here and /goals to check on them.",
             count, noun, joined);

    free(joined);
    return msg;
}
