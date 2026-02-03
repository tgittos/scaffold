#include "approval_gate.h"
#include "pattern_generator.h"

#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "../utils/debug_output.h"
#include "ui/output_formatter.h"
#include "lib/tools/subagent_tool.h"

#define DEBUG_PRINT(...) debug_printf(__VA_ARGS__)
#define DEBUG_ERROR(...) fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n")

#define APPROVAL_TIMEOUT_MS 300000
#define APPROVAL_MSG_MAX_SIZE 65536

/* Not thread-safe, but each subagent is a separate process with its own counter */
static uint32_t g_next_request_id = 1;
static uint32_t next_request_id(void) {
    return g_next_request_id++;
}

static char *format_tool_summary(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->name == NULL) {
        return strdup("[unknown tool]");
    }

    if (strcmp(tool_call->name, "shell") == 0 && tool_call->arguments != NULL) {
        cJSON *args = cJSON_Parse(tool_call->arguments);
        if (args != NULL) {
            cJSON *cmd = cJSON_GetObjectItem(args, "command");
            if (cmd != NULL && cJSON_IsString(cmd)) {
                char *summary = NULL;
                if (asprintf(&summary, "%s: %s", tool_call->name, cmd->valuestring) != -1) {
                    cJSON_Delete(args);
                    return summary;
                }
            }
            cJSON_Delete(args);
        }
    }

    if ((strcmp(tool_call->name, "write_file") == 0 ||
         strcmp(tool_call->name, "read_file") == 0 ||
         strcmp(tool_call->name, "append_file") == 0) &&
        tool_call->arguments != NULL) {
        cJSON *args = cJSON_Parse(tool_call->arguments);
        if (args != NULL) {
            cJSON *path = cJSON_GetObjectItem(args, "path");
            if (path != NULL && cJSON_IsString(path)) {
                char *summary = NULL;
                if (asprintf(&summary, "%s: %s", tool_call->name, path->valuestring) != -1) {
                    cJSON_Delete(args);
                    return summary;
                }
            }
            cJSON_Delete(args);
        }
    }

    if (strcmp(tool_call->name, "web_fetch") == 0 && tool_call->arguments != NULL) {
        cJSON *args = cJSON_Parse(tool_call->arguments);
        if (args != NULL) {
            cJSON *url = cJSON_GetObjectItem(args, "url");
            if (url != NULL && cJSON_IsString(url)) {
                char *summary = NULL;
                if (asprintf(&summary, "%s: %s", tool_call->name, url->valuestring) != -1) {
                    cJSON_Delete(args);
                    return summary;
                }
            }
            cJSON_Delete(args);
        }
    }

    return strdup(tool_call->name);
}

static char *serialize_approval_request(const ApprovalRequest *req) {
    if (req == NULL) {
        return NULL;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(json, "tool_name", req->tool_name ? req->tool_name : "");
    cJSON_AddStringToObject(json, "arguments_json", req->arguments_json ? req->arguments_json : "");
    cJSON_AddStringToObject(json, "display_summary", req->display_summary ? req->display_summary : "");
    cJSON_AddNumberToObject(json, "request_id", req->request_id);

    char *result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

static int deserialize_approval_request(const char *json_str, ApprovalRequest *req) {
    if (json_str == NULL || req == NULL) {
        return -1;
    }

    memset(req, 0, sizeof(*req));

    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL) {
        return -1;
    }

    cJSON *tool_name = cJSON_GetObjectItem(json, "tool_name");
    cJSON *arguments_json = cJSON_GetObjectItem(json, "arguments_json");
    cJSON *display_summary = cJSON_GetObjectItem(json, "display_summary");
    cJSON *request_id = cJSON_GetObjectItem(json, "request_id");

    if (!cJSON_IsString(tool_name) || !cJSON_IsNumber(request_id)) {
        cJSON_Delete(json);
        return -1;
    }

    req->tool_name = strdup(tool_name->valuestring);
    req->arguments_json = cJSON_IsString(arguments_json) ? strdup(arguments_json->valuestring) : strdup("");
    req->display_summary = cJSON_IsString(display_summary) ? strdup(display_summary->valuestring) : strdup("");
    req->request_id = (uint32_t)request_id->valuedouble;

    if (req->tool_name == NULL || req->arguments_json == NULL || req->display_summary == NULL) {
        free(req->tool_name);
        free(req->arguments_json);
        free(req->display_summary);
        memset(req, 0, sizeof(*req));
        cJSON_Delete(json);
        return -1;
    }

    cJSON_Delete(json);
    return 0;
}

static void free_approval_request(ApprovalRequest *req) {
    if (req != NULL) {
        free(req->tool_name);
        free(req->arguments_json);
        free(req->display_summary);
        memset(req, 0, sizeof(*req));
    }
}

static char *serialize_approval_response(const ApprovalResponse *resp) {
    if (resp == NULL) {
        return NULL;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(json, "request_id", resp->request_id);
    cJSON_AddNumberToObject(json, "result", (int)resp->result);
    cJSON_AddStringToObject(json, "pattern", resp->pattern ? resp->pattern : "");

    char *result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

static int deserialize_approval_response(const char *json_str, ApprovalResponse *resp) {
    if (json_str == NULL || resp == NULL) {
        return -1;
    }

    memset(resp, 0, sizeof(*resp));

    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL) {
        return -1;
    }

    cJSON *request_id = cJSON_GetObjectItem(json, "request_id");
    cJSON *result = cJSON_GetObjectItem(json, "result");
    cJSON *pattern = cJSON_GetObjectItem(json, "pattern");

    if (!cJSON_IsNumber(request_id) || !cJSON_IsNumber(result)) {
        cJSON_Delete(json);
        return -1;
    }

    resp->request_id = (uint32_t)request_id->valuedouble;
    resp->result = (ApprovalResult)(int)result->valuedouble;
    resp->pattern = (cJSON_IsString(pattern) && strlen(pattern->valuestring) > 0)
                        ? strdup(pattern->valuestring)
                        : NULL;

    cJSON_Delete(json);
    return 0;
}

static void free_approval_response(ApprovalResponse *resp) {
    if (resp != NULL) {
        free(resp->pattern);
        memset(resp, 0, sizeof(*resp));
    }
}

static char *read_message_with_timeout(int fd, int timeout_ms) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};

    int ready = poll(&pfd, 1, timeout_ms);
    if (ready <= 0) {
        return NULL;
    }

    char *buffer = malloc(APPROVAL_MSG_MAX_SIZE);
    if (buffer == NULL) {
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < APPROVAL_MSG_MAX_SIZE - 1) {
        pfd.revents = 0;
        ready = poll(&pfd, 1, 100);
        if (ready <= 0) {
            break;
        }

        ssize_t n = read(fd, buffer + total_read, APPROVAL_MSG_MAX_SIZE - 1 - total_read);
        if (n <= 0) {
            if (total_read == 0) {
                free(buffer);
                return NULL;
            }
            break;
        }

        for (ssize_t i = 0; i < n; i++) {
            if (buffer[total_read + i] == '\0') {
                total_read += i + 1;
                buffer[total_read - 1] = '\0';
                return buffer;
            }
        }
        total_read += n;
    }

    buffer[total_read] = '\0';
    return buffer;
}

static int write_message(int fd, const char *msg) {
    if (msg == NULL) {
        return -1;
    }

    size_t len = strlen(msg) + 1; /* include null terminator */
    size_t written = 0;

    while (written < len) {
        ssize_t n = write(fd, msg + written, len - written);
        if (n <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        written += n;
    }

    return 0;
}

ApprovalResult subagent_request_approval(const ApprovalChannel *channel,
                                         const ToolCall *tool_call,
                                         ApprovedPath *out_path) {
    if (channel == NULL || tool_call == NULL) {
        DEBUG_ERROR("subagent_request_approval: NULL channel or tool_call");
        return APPROVAL_DENIED;
    }

    if (out_path != NULL) {
        memset(out_path, 0, sizeof(*out_path));
    }

    ApprovalRequest req = {
        .tool_name = (char *)tool_call->name,
        .arguments_json = (char *)tool_call->arguments,
        .display_summary = format_tool_summary(tool_call),
        .request_id = next_request_id()
    };

    if (req.display_summary == NULL) {
        req.display_summary = strdup("[unknown]");
    }

    char *serialized = serialize_approval_request(&req);
    free(req.display_summary);

    if (serialized == NULL) {
        DEBUG_ERROR("subagent_request_approval: Failed to serialize request");
        return APPROVAL_DENIED;
    }

    DEBUG_PRINT("Subagent sending approval request: %s", serialized);

    if (write_message(channel->request_fd, serialized) < 0) {
        DEBUG_ERROR("subagent_request_approval: Failed to write request (errno=%d)", errno);
        free(serialized);
        return APPROVAL_DENIED;
    }
    free(serialized);

    char *response_str = read_message_with_timeout(channel->response_fd, APPROVAL_TIMEOUT_MS);
    if (response_str == NULL) {
        DEBUG_ERROR("subagent_request_approval: Timeout or error waiting for response");
        return APPROVAL_DENIED;
    }

    DEBUG_PRINT("Subagent received response: %s", response_str);

    ApprovalResponse resp;
    if (deserialize_approval_response(response_str, &resp) < 0) {
        DEBUG_ERROR("subagent_request_approval: Failed to parse response");
        free(response_str);
        return APPROVAL_DENIED;
    }
    free(response_str);

    if (resp.request_id != req.request_id) {
        DEBUG_ERROR("subagent_request_approval: Response request_id mismatch");
        free_approval_response(&resp);
        return APPROVAL_DENIED;
    }

    ApprovalResult result = resp.result;

    /* If parent approved with "allow always" and generated a pattern,
     * the pattern has already been added to parent's session allowlist.
     * Subagent doesn't inherit session allowlist, so we just proceed. */
    if (result == APPROVAL_ALLOWED_ALWAYS && resp.pattern != NULL) {
        DEBUG_PRINT("Parent added pattern to allowlist: %s", resp.pattern);
    }

    free_approval_response(&resp);
    return result;
}

int handle_subagent_approval_request(ApprovalGateConfig *config,
                                     ApprovalChannel *channel,
                                     const char *subagent_id) {
    if (config == NULL || channel == NULL) {
        DEBUG_ERROR("handle_subagent_approval_request: NULL config or channel");
        return -1;
    }

    char *request_str = read_message_with_timeout(channel->request_fd, 1000);
    if (request_str == NULL) {
        /* Pipe closed or timeout - expected when subagent completes normally. */
        return -1;
    }

    DEBUG_PRINT("Parent received subagent request: %s", request_str);

    ApprovalRequest req;
    if (deserialize_approval_request(request_str, &req) < 0) {
        DEBUG_ERROR("handle_subagent_approval_request: Failed to parse request");
        free(request_str);
        return -1;
    }
    free(request_str);

    ToolCall synthetic_call = {
        .name = req.tool_name,
        .arguments = req.arguments_json,
        .id = "subagent-synthetic"
    };

    ApprovedPath approved_path = {0};
    ApprovalResult result;

    /* Forward up the chain if we're a nested subagent; only the root
     * process owns the TTY and can prompt the user. */
    ApprovalChannel *our_channel = subagent_get_approval_channel();
    if (our_channel != NULL) {
        DEBUG_PRINT("Nested subagent: forwarding request to grandparent");
        result = subagent_request_approval(our_channel, &synthetic_call, &approved_path);
    } else {
        result = approval_gate_prompt(config, &synthetic_call, &approved_path);
    }

    /* Log the approval decision persistently */
    if (subagent_id != NULL) {
        log_subagent_approval(subagent_id, req.tool_name,
                              req.display_summary, (int)result);
    }

    ApprovalResponse resp = {
        .request_id = req.request_id,
        .result = result,
        .pattern = NULL
    };

    if (result == APPROVAL_ALLOWED_ALWAYS) {
        GeneratedPattern gen_pattern = {0};
        if (generate_allowlist_pattern(&synthetic_call, &gen_pattern) == 0) {
            /* Add pattern to parent's allowlist so future requests auto-approve */
            apply_generated_pattern(config, synthetic_call.name, &gen_pattern);
            if (gen_pattern.pattern != NULL) {
                resp.pattern = strdup(gen_pattern.pattern);
            }
            free_generated_pattern(&gen_pattern);
        }
    }

    char *response_str = serialize_approval_response(&resp);
    free(resp.pattern);
    free_approval_request(&req);
    free_approved_path(&approved_path);

    if (response_str == NULL) {
        DEBUG_ERROR("handle_subagent_approval_request: Failed to serialize response");
        return -1;
    }

    DEBUG_PRINT("Parent sending response: %s", response_str);

    if (write_message(channel->response_fd, response_str) < 0) {
        DEBUG_ERROR("handle_subagent_approval_request: Failed to write response (errno=%d)", errno);
        free(response_str);
        return -1;
    }
    free(response_str);
    return 0;
}

void free_approval_channel(ApprovalChannel *channel) {
    if (channel == NULL) {
        return;
    }

    if (channel->request_fd >= 0) {
        close(channel->request_fd);
    }

    if (channel->response_fd >= 0) {
        close(channel->response_fd);
    }

    free(channel);
}

int create_approval_channel_pipes(int request_pipe[2], int response_pipe[2]) {
    if (request_pipe == NULL || response_pipe == NULL) {
        return -1;
    }

    if (pipe(request_pipe) < 0) {
        DEBUG_ERROR("create_approval_channel_pipes: pipe() failed for request (errno=%d)", errno);
        return -1;
    }

    if (pipe(response_pipe) < 0) {
        DEBUG_ERROR("create_approval_channel_pipes: pipe() failed for response (errno=%d)", errno);
        close(request_pipe[0]);
        close(request_pipe[1]);
        return -1;
    }

    int flags = fcntl(request_pipe[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(request_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    flags = fcntl(response_pipe[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(response_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    return 0;
}

void setup_subagent_channel_child(ApprovalChannel *channel,
                                  int request_pipe[2],
                                  int response_pipe[2]) {
    if (channel == NULL) {
        return;
    }

    /* Child writes requests, reads responses */
    close(request_pipe[0]);   /* Close read end of request pipe */
    close(response_pipe[1]);  /* Close write end of response pipe */

    channel->request_fd = request_pipe[1];   /* Child writes requests */
    channel->response_fd = response_pipe[0]; /* Child reads responses */
    channel->subagent_pid = getpid();
}

void setup_subagent_channel_parent(ApprovalChannel *channel,
                                   int request_pipe[2],
                                   int response_pipe[2],
                                   pid_t child_pid) {
    if (channel == NULL) {
        return;
    }

    /* Parent reads requests, writes responses */
    close(request_pipe[1]);   /* Close write end of request pipe */
    close(response_pipe[0]);  /* Close read end of response pipe */

    channel->request_fd = request_pipe[0];   /* Parent reads requests */
    channel->response_fd = response_pipe[1]; /* Parent writes responses */
    channel->subagent_pid = child_pid;
}

void cleanup_approval_channel_pipes(int request_pipe[2], int response_pipe[2]) {
    if (request_pipe != NULL) {
        if (request_pipe[0] >= 0) close(request_pipe[0]);
        if (request_pipe[1] >= 0) close(request_pipe[1]);
    }
    if (response_pipe != NULL) {
        if (response_pipe[0] >= 0) close(response_pipe[0]);
        if (response_pipe[1] >= 0) close(response_pipe[1]);
    }
}

int poll_subagent_approval_requests(ApprovalChannel *channels,
                                    int channel_count,
                                    int timeout_ms) {
    if (channels == NULL || channel_count <= 0) {
        return -1;
    }

    struct pollfd *pfds = malloc(channel_count * sizeof(struct pollfd));
    if (pfds == NULL) {
        return -1;
    }

    for (int i = 0; i < channel_count; i++) {
        pfds[i].fd = channels[i].request_fd;
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }

    int ready = poll(pfds, channel_count, timeout_ms);
    if (ready <= 0) {
        free(pfds);
        return -1;
    }

    int result = -1;
    for (int i = 0; i < channel_count; i++) {
        if (pfds[i].revents & POLLIN) {
            result = i;
            break;
        }
    }

    free(pfds);
    return result;
}

int parent_approval_loop(ApprovalGateConfig *config,
                         ApprovalChannel *channels,
                         int channel_count,
                         int timeout_ms) {
    if (config == NULL || channels == NULL || channel_count <= 0) {
        return -1;
    }

    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);

    while (1) {
        if (timeout_ms > 0) {
            gettimeofday(&current_time, NULL);
            long elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 +
                              (current_time.tv_usec - start_time.tv_usec) / 1000;
            if (elapsed_ms >= timeout_ms) {
                return 0; /* Timeout - normal exit */
            }
        }

        int idx = poll_subagent_approval_requests(channels, channel_count, 100);
        if (idx >= 0) {
            int result = handle_subagent_approval_request(config, &channels[idx], NULL);
            if (result < 0) {
                /* Pipe broken - close FDs to prevent further polling */
                if (channels[idx].request_fd >= 0) {
                    close(channels[idx].request_fd);
                    channels[idx].request_fd = -1;
                }
                if (channels[idx].response_fd >= 0) {
                    close(channels[idx].response_fd);
                    channels[idx].response_fd = -1;
                }
            }
        }

        int open_channels = 0;
        for (int i = 0; i < channel_count; i++) {
            if (channels[i].request_fd >= 0) {
                open_channels++;
            }
        }
        if (open_channels == 0) {
            return 0; /* All subagents done */
        }
    }
}
