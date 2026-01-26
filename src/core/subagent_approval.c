/**
 * Subagent Approval Proxy Implementation
 *
 * This module provides IPC-based approval proxying for subagents.
 * The parent process maintains exclusive TTY ownership while subagents
 * send approval requests via pipes. This prevents deadlocks that would
 * occur if both parent and subagent tried to access the TTY simultaneously.
 *
 * Architecture:
 * - Parent creates request/response pipes when spawning subagent
 * - Subagent sends ApprovalRequest serialized as JSON via request pipe
 * - Parent reads request, prompts user via TTY, sends ApprovalResponse
 * - Subagent blocks waiting for response with timeout (5 minutes)
 *
 * See SPEC_APPROVAL_GATES.md section "Subagent Behavior > Subagent Deadlock Prevention"
 */

#include "approval_gate.h"

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

/* =============================================================================
 * Debug Macros
 * ========================================================================== */

/* Debug print for info messages - uses debug_printf which checks debug_enabled */
#define DEBUG_PRINT(...) debug_printf(__VA_ARGS__)

/* Debug print for errors - always output to stderr */
#define DEBUG_ERROR(...) fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n")

/* =============================================================================
 * Constants
 * ========================================================================== */

/* Timeout for subagent waiting for parent response (5 minutes) */
#define APPROVAL_TIMEOUT_MS 300000

/* Maximum size for serialized approval messages */
#define APPROVAL_MSG_MAX_SIZE 65536

/* Request ID counter (simple incrementing integer).
 * IMPORTANT: This counter is NOT thread-safe. All calls to next_request_id()
 * must occur from the same thread. In the current architecture, subagents
 * are separate processes (not threads), so each process has its own counter. */
static uint32_t g_next_request_id = 1;

/* =============================================================================
 * Internal Helper Functions
 * ========================================================================== */

/**
 * Generate the next unique request ID.
 * Uses simple incrementing counter.
 *
 * NOTE: Not thread-safe. Must only be called from a single thread.
 * This is safe in the current process-based subagent architecture.
 */
static uint32_t next_request_id(void) {
    return g_next_request_id++;
}

/**
 * Format a tool call into a human-readable summary string.
 * Returns allocated string that caller must free.
 */
static char *format_tool_summary(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->name == NULL) {
        return strdup("[unknown tool]");
    }

    /* For shell commands, extract the command argument */
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

    /* For file operations, extract the path argument */
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

    /* For network operations, extract the URL */
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

    /* Default: just the tool name */
    return strdup(tool_call->name);
}

/**
 * Serialize an ApprovalRequest to JSON string.
 * Returns allocated string that caller must free, or NULL on error.
 */
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

/**
 * Deserialize an ApprovalRequest from JSON string.
 * Returns 0 on success, -1 on error.
 */
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

/**
 * Free resources held by an ApprovalRequest.
 */
static void free_approval_request(ApprovalRequest *req) {
    if (req != NULL) {
        free(req->tool_name);
        free(req->arguments_json);
        free(req->display_summary);
        memset(req, 0, sizeof(*req));
    }
}

/**
 * Serialize an ApprovalResponse to JSON string.
 * Returns allocated string that caller must free, or NULL on error.
 */
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

/**
 * Deserialize an ApprovalResponse from JSON string.
 * Returns 0 on success, -1 on error.
 */
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

/**
 * Free resources held by an ApprovalResponse.
 */
static void free_approval_response(ApprovalResponse *resp) {
    if (resp != NULL) {
        free(resp->pattern);
        memset(resp, 0, sizeof(*resp));
    }
}

/**
 * Read a message from a file descriptor with timeout.
 * Messages are null-terminated strings.
 * Returns allocated buffer on success, NULL on error or timeout.
 */
static char *read_message_with_timeout(int fd, int timeout_ms) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};

    int ready = poll(&pfd, 1, timeout_ms);
    if (ready <= 0) {
        /* Timeout or error */
        return NULL;
    }

    /* Read message in chunks until we get a null terminator */
    char *buffer = malloc(APPROVAL_MSG_MAX_SIZE);
    if (buffer == NULL) {
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < APPROVAL_MSG_MAX_SIZE - 1) {
        /* Check if more data is available */
        pfd.revents = 0;
        ready = poll(&pfd, 1, 100); /* 100ms timeout for subsequent chunks */
        if (ready <= 0) {
            /* No more data or error */
            break;
        }

        ssize_t n = read(fd, buffer + total_read, APPROVAL_MSG_MAX_SIZE - 1 - total_read);
        if (n <= 0) {
            /* EOF or error */
            if (total_read == 0) {
                free(buffer);
                return NULL;
            }
            break;
        }

        /* Check for null terminator in what we just read */
        for (ssize_t i = 0; i < n; i++) {
            if (buffer[total_read + i] == '\0') {
                /* Found terminator - message complete */
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

/**
 * Write a message to a file descriptor with null terminator.
 * Returns 0 on success, -1 on error.
 */
static int write_message(int fd, const char *msg) {
    if (msg == NULL) {
        return -1;
    }

    size_t len = strlen(msg) + 1; /* Include null terminator */
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

/* =============================================================================
 * Public Functions
 * ========================================================================== */

/**
 * Request approval from parent process (subagent side).
 *
 * This function is called by a subagent when it needs approval for a tool call.
 * It serializes the request, sends it to the parent via pipe, and blocks
 * waiting for the response with a 5-minute timeout.
 *
 * @param channel IPC channel to parent
 * @param tool_call The tool call requiring approval
 * @param out_path Output: path verification data (currently not populated by proxy)
 * @return Parent's decision (DENIED on timeout or error)
 */
ApprovalResult subagent_request_approval(const ApprovalChannel *channel,
                                         const ToolCall *tool_call,
                                         ApprovedPath *out_path) {
    if (channel == NULL || tool_call == NULL) {
        DEBUG_ERROR("subagent_request_approval: NULL channel or tool_call");
        return APPROVAL_DENIED;
    }

    /* Initialize out_path if provided */
    if (out_path != NULL) {
        memset(out_path, 0, sizeof(*out_path));
    }

    /* Build the approval request */
    ApprovalRequest req = {
        .tool_name = (char *)tool_call->name,
        .arguments_json = (char *)tool_call->arguments,
        .display_summary = format_tool_summary(tool_call),
        .request_id = next_request_id()
    };

    if (req.display_summary == NULL) {
        req.display_summary = strdup("[unknown]");
    }

    /* Serialize and send to parent */
    char *serialized = serialize_approval_request(&req);
    free(req.display_summary); /* We allocated this */

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

    /* Block waiting for parent response with timeout */
    char *response_str = read_message_with_timeout(channel->response_fd, APPROVAL_TIMEOUT_MS);
    if (response_str == NULL) {
        DEBUG_ERROR("subagent_request_approval: Timeout or error waiting for response");
        return APPROVAL_DENIED;
    }

    DEBUG_PRINT("Subagent received response: %s", response_str);

    /* Parse the response */
    ApprovalResponse resp;
    if (deserialize_approval_response(response_str, &resp) < 0) {
        DEBUG_ERROR("subagent_request_approval: Failed to parse response");
        free(response_str);
        return APPROVAL_DENIED;
    }
    free(response_str);

    /* Verify request ID matches (basic sanity check) */
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

/**
 * Handle approval request from subagent (parent side).
 *
 * This function is called by the parent when it detects data on a subagent's
 * request pipe. It reads the request, prompts the user via TTY, and sends
 * the response back to the subagent.
 *
 * @param config Parent's gate configuration (may be modified if ALLOWED_ALWAYS)
 * @param channel IPC channel to subagent
 */
void handle_subagent_approval_request(ApprovalGateConfig *config,
                                      ApprovalChannel *channel) {
    if (config == NULL || channel == NULL) {
        DEBUG_ERROR("handle_subagent_approval_request: NULL config or channel");
        return;
    }

    /* Read the request from subagent */
    char *request_str = read_message_with_timeout(channel->request_fd, 1000);
    if (request_str == NULL) {
        DEBUG_ERROR("handle_subagent_approval_request: Failed to read request");
        return;
    }

    DEBUG_PRINT("Parent received subagent request: %s", request_str);

    /* Parse the request */
    ApprovalRequest req;
    if (deserialize_approval_request(request_str, &req) < 0) {
        DEBUG_ERROR("handle_subagent_approval_request: Failed to parse request");
        free(request_str);
        return;
    }
    free(request_str);

    /* Display prompt to user (parent owns TTY) */
    printf("\n");
    printf("┌─ Subagent Approval Required ─────────────────────────────────┐\n");
    printf("│  PID: %-55d│\n", channel->subagent_pid);
    printf("│  Tool: %-54s│\n", req.tool_name ? req.tool_name : "[unknown]");
    printf("│  %-61s│\n", req.display_summary ? req.display_summary : "");
    printf("│                                                               │\n");
    printf("│  [y] Allow  [n] Deny  [a] Allow always  [?] Details           │\n");
    printf("└───────────────────────────────────────────────────────────────┘\n");
    fflush(stdout);

    /* Create a synthetic ToolCall for prompting */
    ToolCall synthetic_call = {
        .name = req.tool_name,
        .arguments = req.arguments_json,
        .id = "subagent-synthetic"
    };

    /* Get user response using the standard prompt mechanism */
    ApprovedPath approved_path = {0};
    ApprovalResult result = approval_gate_prompt(config, &synthetic_call, &approved_path);

    /* Build the response */
    ApprovalResponse resp = {
        .request_id = req.request_id,
        .result = result,
        .pattern = NULL
    };

    /* If user selected "allow always", generate and apply pattern */
    if (result == APPROVAL_ALLOWED_ALWAYS) {
        GeneratedPattern gen_pattern = {0};
        if (generate_allowlist_pattern(&synthetic_call, &gen_pattern) == 0) {
            if (gen_pattern.pattern != NULL) {
                resp.pattern = strdup(gen_pattern.pattern);
                /* Pattern is already applied to config by approval_gate_prompt */
            }
            free_generated_pattern(&gen_pattern);
        }
    }

    /* Serialize and send response */
    char *response_str = serialize_approval_response(&resp);
    free(resp.pattern);
    free_approval_request(&req);
    free_approved_path(&approved_path);

    if (response_str == NULL) {
        DEBUG_ERROR("handle_subagent_approval_request: Failed to serialize response");
        return;
    }

    DEBUG_PRINT("Parent sending response: %s", response_str);

    if (write_message(channel->response_fd, response_str) < 0) {
        DEBUG_ERROR("handle_subagent_approval_request: Failed to write response (errno=%d)", errno);
    }
    free(response_str);
}

/**
 * Free resources held by an ApprovalChannel.
 *
 * Closes both file descriptors if they are valid, then frees the struct.
 *
 * @param channel Channel to clean up (will be freed)
 */
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

/* =============================================================================
 * Pipe Creation for Subagent Spawning
 * ========================================================================== */

/**
 * Create approval channel pipes for a new subagent.
 *
 * Creates two pipes:
 * - request_pipe: subagent writes, parent reads
 * - response_pipe: parent writes, subagent reads
 *
 * After fork:
 * - Child calls setup_subagent_channel_child() to get its channel
 * - Parent calls setup_subagent_channel_parent() to get its channel
 *
 * @param request_pipe Output: pipe for requests [0]=read, [1]=write
 * @param response_pipe Output: pipe for responses [0]=read, [1]=write
 * @return 0 on success, -1 on error
 */
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

    /* Set non-blocking on read ends for polling */
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

/**
 * Set up the approval channel for the subagent (child) process.
 *
 * Closes parent ends of pipes and initializes channel struct.
 * Call this in the child process after fork().
 *
 * @param channel Output: channel struct to initialize
 * @param request_pipe The request pipe from create_approval_channel_pipes()
 * @param response_pipe The response pipe from create_approval_channel_pipes()
 */
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

/**
 * Set up the approval channel for the parent process.
 *
 * Closes child ends of pipes and initializes channel struct.
 * Call this in the parent process after fork().
 *
 * @param channel Output: channel struct to initialize
 * @param request_pipe The request pipe from create_approval_channel_pipes()
 * @param response_pipe The response pipe from create_approval_channel_pipes()
 * @param child_pid The PID of the child process
 */
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

/**
 * Close all pipe ends and clean up after failed fork/spawn.
 *
 * @param request_pipe The request pipe
 * @param response_pipe The response pipe
 */
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

/* =============================================================================
 * Parent Approval Loop Support
 * ========================================================================== */

/**
 * Check if any subagent has a pending approval request.
 *
 * Uses poll() to check if data is available on any subagent request pipe.
 * This is a non-blocking check suitable for integration into a main loop.
 *
 * @param channels Array of approval channels for active subagents
 * @param channel_count Number of channels in the array
 * @param timeout_ms Maximum time to wait in milliseconds (0 for non-blocking)
 * @return Index of channel with pending request, or -1 if none (or error)
 */
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

    /* Find the first channel with data */
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

/**
 * Run the parent approval loop.
 *
 * Monitors all subagent request pipes using poll().
 * Handles interleaved approvals from multiple concurrent subagents.
 *
 * This function runs continuously until:
 * - All subagent channels are closed
 * - An error occurs
 * - The timeout expires
 *
 * @param config Parent's gate configuration
 * @param channels Array of approval channels for active subagents
 * @param channel_count Number of channels in the array
 * @param timeout_ms Maximum time to run the loop (0 for indefinite)
 * @return 0 on normal exit, -1 on error
 */
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
        /* Check timeout */
        if (timeout_ms > 0) {
            gettimeofday(&current_time, NULL);
            long elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 +
                              (current_time.tv_usec - start_time.tv_usec) / 1000;
            if (elapsed_ms >= timeout_ms) {
                return 0; /* Timeout - normal exit */
            }
        }

        /* Poll for requests with 100ms timeout */
        int idx = poll_subagent_approval_requests(channels, channel_count, 100);
        if (idx >= 0) {
            handle_subagent_approval_request(config, &channels[idx]);
        }

        /* Check if all channels are closed */
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
