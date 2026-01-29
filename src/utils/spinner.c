#include "spinner.h"
#include "output_formatter.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cJSON.h>

#define SPINNER_INTERVAL_MS 300
#define ARG_DISPLAY_MAX_LEN 50

static pthread_t g_spinner_thread;
static pthread_mutex_t g_spinner_mutex;
static pthread_cond_t g_spinner_cond;
static pthread_once_t g_spinner_init_once = PTHREAD_ONCE_INIT;
static volatile int g_spinner_running = 0;
static volatile int g_spinner_initialized = 0;
static char *g_spinner_tool_name = NULL;
static char *g_spinner_arg_summary = NULL;

static void spinner_do_init(void) {
    pthread_mutex_init(&g_spinner_mutex, NULL);
    pthread_cond_init(&g_spinner_cond, NULL);
    g_spinner_initialized = 1;
}

static char *extract_spinner_arg_summary(const char *tool_name, const char *arguments) {
    if (arguments == NULL || strlen(arguments) == 0) {
        return NULL;
    }

    cJSON *json = cJSON_Parse(arguments);
    if (json == NULL) {
        return NULL;
    }

    char summary[256];
    memset(summary, 0, sizeof(summary));
    const char *value = NULL;
    const char *label = "";

    cJSON *path = cJSON_GetObjectItem(json, "path");
    cJSON *file_path = cJSON_GetObjectItem(json, "file_path");
    cJSON *directory_path = cJSON_GetObjectItem(json, "directory_path");
    cJSON *command = cJSON_GetObjectItem(json, "command");
    cJSON *url = cJSON_GetObjectItem(json, "url");
    cJSON *query = cJSON_GetObjectItem(json, "query");
    cJSON *pattern = cJSON_GetObjectItem(json, "pattern");
    cJSON *key = cJSON_GetObjectItem(json, "key");
    cJSON *collection = cJSON_GetObjectItem(json, "collection");
    cJSON *text = cJSON_GetObjectItem(json, "text");

    if (tool_name != NULL && (strcmp(tool_name, "shell") == 0 || strcmp(tool_name, "shell_execute") == 0)) {
        if (command != NULL && cJSON_IsString(command)) {
            value = cJSON_GetStringValue(command);
            label = "";
        }
    } else if (tool_name != NULL && strcmp(tool_name, "search_files") == 0) {
        cJSON *regex_pattern = cJSON_GetObjectItem(json, "regex_pattern");
        if (regex_pattern != NULL && cJSON_IsString(regex_pattern)) {
            const char *pattern_val = cJSON_GetStringValue(regex_pattern);
            const char *path_val = (path != NULL && cJSON_IsString(path)) ? cJSON_GetStringValue(path) : ".";
            size_t pattern_len = strlen(pattern_val);
            if (pattern_len <= ARG_DISPLAY_MAX_LEN - 10) {
                snprintf(summary, sizeof(summary), "%s → /%s/", path_val, pattern_val);
            } else {
                snprintf(summary, sizeof(summary), "%s → /%.37s.../", path_val, pattern_val);
            }
            cJSON_Delete(json);
            return strdup(summary);
        }
    } else if (tool_name != NULL && strstr(tool_name, "write") != NULL) {
        if (path != NULL && cJSON_IsString(path)) {
            value = cJSON_GetStringValue(path);
        } else if (file_path != NULL && cJSON_IsString(file_path)) {
            value = cJSON_GetStringValue(file_path);
        }
    } else if (path != NULL && cJSON_IsString(path)) {
        value = cJSON_GetStringValue(path);
    } else if (file_path != NULL && cJSON_IsString(file_path)) {
        value = cJSON_GetStringValue(file_path);
    } else if (directory_path != NULL && cJSON_IsString(directory_path)) {
        value = cJSON_GetStringValue(directory_path);
    } else if (command != NULL && cJSON_IsString(command)) {
        value = cJSON_GetStringValue(command);
    } else if (url != NULL && cJSON_IsString(url)) {
        value = cJSON_GetStringValue(url);
    } else if (query != NULL && cJSON_IsString(query)) {
        value = cJSON_GetStringValue(query);
        label = "query: ";
    } else if (pattern != NULL && cJSON_IsString(pattern)) {
        value = cJSON_GetStringValue(pattern);
        label = "pattern: ";
    } else if (key != NULL && cJSON_IsString(key)) {
        value = cJSON_GetStringValue(key);
        label = "key: ";
    } else if (collection != NULL && cJSON_IsString(collection)) {
        value = cJSON_GetStringValue(collection);
        label = "collection: ";
    } else if (text != NULL && cJSON_IsString(text)) {
        value = cJSON_GetStringValue(text);
        label = "text: ";
    }

    if (value == NULL) {
        cJSON *subject = cJSON_GetObjectItem(json, "subject");
        cJSON *taskId = cJSON_GetObjectItem(json, "taskId");
        cJSON *status = cJSON_GetObjectItem(json, "status");
        cJSON *content_field = cJSON_GetObjectItem(json, "content");

        if (subject != NULL && cJSON_IsString(subject)) {
            value = cJSON_GetStringValue(subject);
        } else if (taskId != NULL && cJSON_IsString(taskId)) {
            const char *task_id_val = cJSON_GetStringValue(taskId);
            if (status != NULL && cJSON_IsString(status)) {
                const char *status_val = cJSON_GetStringValue(status);
                snprintf(summary, sizeof(summary), "#%s → %s", task_id_val, status_val);
                cJSON_Delete(json);
                return strdup(summary);
            } else {
                value = task_id_val;
                label = "#";
            }
        } else if (content_field != NULL && cJSON_IsString(content_field)) {
            value = cJSON_GetStringValue(content_field);
        }
    }

    if (value != NULL && strlen(value) > 0) {
        size_t value_len = strlen(value);
        if (value_len <= ARG_DISPLAY_MAX_LEN) {
            snprintf(summary, sizeof(summary), "%s%s", label, value);
        } else {
            snprintf(summary, sizeof(summary), "%s%.47s...", label, value);
        }
    }

    cJSON_Delete(json);

    if (strlen(summary) > 0) {
        return strdup(summary);
    }
    return NULL;
}

static void *spinner_thread_func(void *arg) {
    (void)arg;

    int bright = 1;

    pthread_mutex_lock(&g_spinner_mutex);
    while (g_spinner_running) {
        char context[128];
        memset(context, 0, sizeof(context));

        if (g_spinner_arg_summary != NULL && strlen(g_spinner_arg_summary) > 0) {
            snprintf(context, sizeof(context), " (%s)", g_spinner_arg_summary);
        }

        if (bright) {
            fprintf(stdout, "\r\033[K\033[36m•\033[0m %s\033[2m%s\033[0m",
                    g_spinner_tool_name ? g_spinner_tool_name : "",
                    context);
        } else {
            fprintf(stdout, "\r\033[K\033[2;36m•\033[0m %s\033[2m%s\033[0m",
                    g_spinner_tool_name ? g_spinner_tool_name : "",
                    context);
        }
        fflush(stdout);

        bright = !bright;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += SPINNER_INTERVAL_MS * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        pthread_cond_timedwait(&g_spinner_cond, &g_spinner_mutex, &ts);
    }
    pthread_mutex_unlock(&g_spinner_mutex);

    return NULL;
}

void spinner_start(const char *tool_name, const char *arguments) {
    if (get_json_output_mode()) {
        return;
    }

    pthread_once(&g_spinner_init_once, spinner_do_init);
    pthread_mutex_lock(&g_spinner_mutex);

    if (g_spinner_running) {
        pthread_mutex_unlock(&g_spinner_mutex);
        return;
    }

    if (g_spinner_tool_name != NULL) {
        free(g_spinner_tool_name);
        g_spinner_tool_name = NULL;
    }
    if (g_spinner_arg_summary != NULL) {
        free(g_spinner_arg_summary);
        g_spinner_arg_summary = NULL;
    }

    g_spinner_tool_name = tool_name ? strdup(tool_name) : NULL;
    g_spinner_arg_summary = extract_spinner_arg_summary(tool_name, arguments);

    g_spinner_running = 1;

    if (pthread_create(&g_spinner_thread, NULL, spinner_thread_func, NULL) != 0) {
        g_spinner_running = 0;
        pthread_mutex_unlock(&g_spinner_mutex);
        return;
    }

    pthread_mutex_unlock(&g_spinner_mutex);
}

void spinner_stop(void) {
    if (!g_spinner_initialized) {
        return;
    }

    pthread_mutex_lock(&g_spinner_mutex);

    if (!g_spinner_running) {
        pthread_mutex_unlock(&g_spinner_mutex);
        return;
    }

    g_spinner_running = 0;
    pthread_cond_signal(&g_spinner_cond);
    pthread_mutex_unlock(&g_spinner_mutex);

    pthread_join(g_spinner_thread, NULL);

    if (!get_json_output_mode()) {
        fprintf(stdout, "\r\033[K");
        fflush(stdout);
    }

    pthread_mutex_lock(&g_spinner_mutex);
    if (g_spinner_tool_name != NULL) {
        free(g_spinner_tool_name);
        g_spinner_tool_name = NULL;
    }
    if (g_spinner_arg_summary != NULL) {
        free(g_spinner_arg_summary);
        g_spinner_arg_summary = NULL;
    }
    pthread_mutex_unlock(&g_spinner_mutex);
}

void spinner_cleanup(void) {
    spinner_stop();
    if (g_spinner_initialized) {
        pthread_mutex_destroy(&g_spinner_mutex);
        pthread_cond_destroy(&g_spinner_cond);
        g_spinner_initialized = 0;
    }
}
