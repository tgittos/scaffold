#include "spinner.h"
#include "output_formatter.h"
#include "terminal.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SPINNER_INTERVAL_MS 300

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
            fprintf(stdout, TERM_CLEAR_LINE TERM_CYAN TERM_SYM_ACTIVE TERM_RESET " %s" TERM_DIM "%s" TERM_RESET,
                    g_spinner_tool_name ? g_spinner_tool_name : "",
                    context);
        } else {
            fprintf(stdout, TERM_CLEAR_LINE TERM_DIM TERM_CYAN TERM_SYM_ACTIVE TERM_RESET " %s" TERM_DIM "%s" TERM_RESET,
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
    g_spinner_arg_summary = extract_arg_summary(tool_name, arguments);

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
        fprintf(stdout, TERM_CLEAR_LINE);
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
