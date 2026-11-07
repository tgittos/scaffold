/*
 * session-transcript plugin
 *
 * Produces a human-readable markdown transcript of agent conversations.
 * Subscribes to post_user_input, post_llm_response, post_tool_execute hooks
 * and writes timestamped entries to a per-session markdown file.
 *
 * Configuration via ~/.local/scaffold/transcript.conf (key=value):
 *   output_dir       — where transcripts are saved (default: ~/.local/scaffold/transcripts/)
 *   max_arg_length   — threshold for truncating tool arguments (default: 500)
 *   max_result_length — threshold for truncating tool results (default: 2000)
 *
 * Build with: cosmocc -o session-transcript main.c cJSON.c -I.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <cJSON.h>

#define DEFAULT_MAX_ARG_LENGTH    500
#define DEFAULT_MAX_RESULT_LENGTH 2000
#define LINE_BUF_SIZE             (10 * 1024 * 1024) /* 10 MB max message */

/* Known tools whose arguments are always summarized */
static const char *always_summarize[] = { "apply_patch", NULL };

/* --- State --- */

static FILE *transcript_fp;
static time_t session_start;
static int max_arg_length   = DEFAULT_MAX_ARG_LENGTH;
static int max_result_length = DEFAULT_MAX_RESULT_LENGTH;
static char output_dir[1024];

/* --- Helpers --- */

static void get_default_output_dir(char *buf, size_t len) {
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buf, len, "%s/.local/scaffold/transcripts", home);
    } else {
        snprintf(buf, len, "/tmp/scaffold-transcripts");
    }
}

static void get_config_path(char *buf, size_t len) {
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buf, len, "%s/.local/scaffold/transcript.conf", home);
    } else {
        buf[0] = '\0';
    }
}

static int mkdirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static void load_config(void) {
    get_default_output_dir(output_dir, sizeof(output_dir));

    char config_path[1024];
    get_config_path(config_path, sizeof(config_path));
    if (config_path[0] == '\0') return;

    FILE *f = fopen(config_path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip comments and blanks */
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* Trim leading spaces from value */
        while (*val == ' ') val++;

        if (strcmp(key, "output_dir") == 0) {
            /* Expand ~ */
            if (val[0] == '~' && val[1] == '/') {
                const char *home = getenv("HOME");
                if (home)
                    snprintf(output_dir, sizeof(output_dir), "%s%s", home, val + 1);
                else
                    snprintf(output_dir, sizeof(output_dir), "%s", val);
            } else {
                snprintf(output_dir, sizeof(output_dir), "%s", val);
            }
        } else if (strcmp(key, "max_arg_length") == 0) {
            int v = atoi(val);
            if (v > 0) max_arg_length = v;
        } else if (strcmp(key, "max_result_length") == 0) {
            int v = atoi(val);
            if (v > 0) max_result_length = v;
        }
    }
    fclose(f);
}

static int should_always_summarize(const char *tool_name) {
    for (int i = 0; always_summarize[i]; i++) {
        if (strcmp(tool_name, always_summarize[i]) == 0) return 1;
    }
    return 0;
}

static void write_transcript(const char *fmt, ...) {
    if (!transcript_fp) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(transcript_fp, fmt, ap);
    va_end(ap);
    fflush(transcript_fp);
}

/* Write a string to the transcript, truncated if over max_len */
static void write_truncated(const char *str, int max_len) {
    if (!str || !transcript_fp) return;
    int len = (int)strlen(str);
    if (len <= max_len) {
        fprintf(transcript_fp, "%s", str);
    } else {
        fwrite(str, 1, max_len, transcript_fp);
        fprintf(transcript_fp, "\n... [%d chars total]", len);
    }
    fflush(transcript_fp);
}

/* --- JSON-RPC helpers --- */

static void send_response(const char *json) {
    fprintf(stdout, "%s\n", json);
    fflush(stdout);
}

static void send_json(cJSON *root) {
    char *str = cJSON_PrintUnformatted(root);
    if (str) {
        send_response(str);
        free(str);
    }
    cJSON_Delete(root);
}

static cJSON *make_response(int id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);
    return root;
}

/* --- Hook handlers --- */

static void handle_initialize(int id) {
    load_config();
    mkdirs(output_dir);

    session_start = time(NULL);
    struct tm *tm = localtime(&session_start);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M-%S", tm);

    char filepath[1200];
    snprintf(filepath, sizeof(filepath), "%s/transcript-%s.md", output_dir, ts);

    transcript_fp = fopen(filepath, "w");
    if (transcript_fp) {
        write_transcript("# Session Transcript \xe2\x80\x94 %s\n\n", ts);
    }

    cJSON *root = make_response(id);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "name", "session-transcript");
#ifdef PLUGIN_VERSION
    cJSON_AddStringToObject(result, "version", PLUGIN_VERSION);
#else
    cJSON_AddStringToObject(result, "version", "1.0.0");
#endif
    cJSON_AddStringToObject(result, "description",
                            "Records agent conversations to markdown transcripts");

    cJSON *hooks = cJSON_CreateArray();
    cJSON_AddItemToArray(hooks, cJSON_CreateString("post_user_input"));
    cJSON_AddItemToArray(hooks, cJSON_CreateString("post_llm_response"));
    cJSON_AddItemToArray(hooks, cJSON_CreateString("post_tool_execute"));
    cJSON_AddItemToObject(result, "hooks", hooks);

    cJSON *tools = cJSON_CreateArray();
    cJSON_AddItemToObject(result, "tools", tools);
    cJSON_AddNumberToObject(result, "priority", 900);

    cJSON_AddItemToObject(root, "result", result);
    send_json(root);
}

static void handle_post_user_input(int id, cJSON *params) {
    const char *message = "";
    cJSON *msg = cJSON_GetObjectItem(params, "message");
    if (msg && cJSON_IsString(msg)) message = msg->valuestring;

    write_transcript("## User\n%s\n\n", message);

    cJSON *root = make_response(id);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "action", "continue");
    cJSON_AddStringToObject(result, "message", message);
    cJSON_AddItemToObject(root, "result", result);
    send_json(root);
}

static void handle_post_llm_response(int id, cJSON *params) {
    const char *text = "";
    cJSON *t = cJSON_GetObjectItem(params, "text");
    if (t && cJSON_IsString(t)) text = t->valuestring;

    if (text[0] != '\0') {
        write_transcript("## Assistant\n%s\n\n", text);
    }

    cJSON *tc_array = cJSON_GetObjectItem(params, "tool_calls");
    if (tc_array && cJSON_IsArray(tc_array)) {
        cJSON *tc;
        cJSON_ArrayForEach(tc, tc_array) {
            const char *name = "unknown";
            cJSON *n = cJSON_GetObjectItem(tc, "name");
            if (n && cJSON_IsString(n)) name = n->valuestring;

            cJSON *args = cJSON_GetObjectItem(tc, "arguments");
            const char *args_str = "";
            if (args && cJSON_IsString(args)) args_str = args->valuestring;

            write_transcript("### Tool Call: %s\n**Arguments:** `", name);

            if (should_always_summarize(name)) {
                write_transcript("[%s content, %d chars]", name, (int)strlen(args_str));
            } else {
                write_truncated(args_str, max_arg_length);
            }

            write_transcript("`\n\n");
        }
    }

    cJSON *root = make_response(id);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "action", "continue");
    cJSON_AddStringToObject(result, "text", text);
    cJSON_AddItemToObject(root, "result", result);
    send_json(root);
}

static void handle_post_tool_execute(int id, cJSON *params) {
    const char *name = "unknown";
    cJSON *n = cJSON_GetObjectItem(params, "tool_name");
    if (n && cJSON_IsString(n)) name = n->valuestring;

    const char *result_str = "";
    cJSON *r = cJSON_GetObjectItem(params, "result");
    if (r && cJSON_IsString(r)) result_str = r->valuestring;

    int success = 1;
    cJSON *s = cJSON_GetObjectItem(params, "success");
    if (s) success = cJSON_IsTrue(s);

    write_transcript("### Tool Result: %s (%s)\n> ",
                     name, success ? "success" : "error");
    write_truncated(result_str, max_result_length);
    write_transcript("\n\n");

    cJSON *root = make_response(id);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "action", "continue");
    cJSON_AddStringToObject(res, "result", result_str);
    cJSON_AddItemToObject(root, "result", res);
    send_json(root);
}

static void handle_shutdown(int id) {
    if (transcript_fp) {
        time_t now = time(NULL);
        int elapsed = (int)(now - session_start);
        int minutes = elapsed / 60;
        int seconds = elapsed % 60;
        write_transcript("---\n*Session ended. Duration: %dm %ds*\n", minutes, seconds);
        fclose(transcript_fp);
        transcript_fp = NULL;
    }

    cJSON *root = make_response(id);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", 1);
    cJSON_AddItemToObject(root, "result", result);
    send_json(root);
}

static void handle_unknown(int id) {
    cJSON *root = make_response(id);
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", -32601);
    cJSON_AddStringToObject(err, "message", "Unknown method");
    cJSON_AddItemToObject(root, "error", err);
    send_json(root);
}

/* --- Main loop --- */

int main(void) {
    char *line = malloc(LINE_BUF_SIZE);
    if (!line) return 1;

    while (fgets(line, LINE_BUF_SIZE, stdin)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        cJSON *request = cJSON_Parse(line);
        if (!request) continue;

        cJSON *method_j = cJSON_GetObjectItem(request, "method");
        cJSON *id_j = cJSON_GetObjectItem(request, "id");
        cJSON *params = cJSON_GetObjectItem(request, "params");

        const char *method = (method_j && cJSON_IsString(method_j))
                             ? method_j->valuestring : "";
        int id = (id_j && cJSON_IsNumber(id_j)) ? id_j->valueint : 0;

        if (strcmp(method, "initialize") == 0) {
            handle_initialize(id);
        } else if (strcmp(method, "hook/post_user_input") == 0) {
            handle_post_user_input(id, params);
        } else if (strcmp(method, "hook/post_llm_response") == 0) {
            handle_post_llm_response(id, params);
        } else if (strcmp(method, "hook/post_tool_execute") == 0) {
            handle_post_tool_execute(id, params);
        } else if (strcmp(method, "shutdown") == 0) {
            handle_shutdown(id);
            cJSON_Delete(request);
            break;
        } else {
            handle_unknown(id);
        }

        cJSON_Delete(request);
    }

    free(line);
    return 0;
}
