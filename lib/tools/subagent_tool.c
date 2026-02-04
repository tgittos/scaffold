#include "subagent_tool.h"
#include "subagent_process.h"
#include "../util/json_escape.h"
#include "../util/debug_output.h"
#include "../../src/core/ralph.h"
#include "../util/interrupt.h"
#include "../policy/subagent_approval.h"
#include "../session/conversation_tracker.h"
#include "../ipc/message_store.h"
#include "messaging_tool.h"
#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Environment variable names for passing approval channel FDs to child */
#define RALPH_APPROVAL_REQUEST_FD "RALPH_APPROVAL_REQUEST_FD"
#define RALPH_APPROVAL_RESPONSE_FD "RALPH_APPROVAL_RESPONSE_FD"

DARRAY_DEFINE(SubagentArray, Subagent)

/**
 * Static pointer to the SubagentManager for use by execute functions.
 * Set during tool registration, used by execute_subagent_tool_call and
 * execute_subagent_status_tool_call since those functions can't receive
 * the manager pointer through their signature.
 *
 * THREAD-SAFETY NOTE: This global pointer makes the subagent tool module
 * non-reentrant and not thread-safe. Only one SubagentManager can be active
 * at a time per process. If concurrent sessions were ever needed, this would
 * require refactoring to pass the manager through the tool execution context.
 * For the current single-threaded CLI design, this is acceptable.
 */
static SubagentManager *g_subagent_manager = NULL;

/**
 * Static approval channel for subagent processes.
 * When running as a subagent, this is initialized from environment variables
 * to communicate with the parent process for approval requests.
 */
static ApprovalChannel *g_subagent_approval_channel = NULL;

/**
 * Initialize approval channel from environment variables.
 * Called when ralph is running as a subagent process.
 * Returns 0 on success, -1 on error or if not a subagent.
 *
 * Uses strtol() instead of atoi() to properly detect parse failures.
 * FDs must be > 2 (skip stdin/stdout/stderr) and <= INT_MAX.
 */
int subagent_init_approval_channel(void) {
    const char *request_fd_str = getenv(RALPH_APPROVAL_REQUEST_FD);
    const char *response_fd_str = getenv(RALPH_APPROVAL_RESPONSE_FD);

    if (request_fd_str == NULL || response_fd_str == NULL) {
        /* Not running as subagent or parent didn't set up approval channel */
        return -1;
    }

    char *endptr;
    errno = 0;
    long request_fd_long = strtol(request_fd_str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || request_fd_long <= 2 || request_fd_long > INT_MAX) {
        return -1;
    }

    errno = 0;
    long response_fd_long = strtol(response_fd_str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || response_fd_long <= 2 || response_fd_long > INT_MAX) {
        return -1;
    }

    g_subagent_approval_channel = malloc(sizeof(ApprovalChannel));
    if (g_subagent_approval_channel == NULL) {
        return -1;
    }

    g_subagent_approval_channel->request_fd = (int)request_fd_long;
    g_subagent_approval_channel->response_fd = (int)response_fd_long;
    g_subagent_approval_channel->subagent_pid = getpid();

    return 0;
}

/** Clean up the subagent approval channel. */
void subagent_cleanup_approval_channel(void) {
    if (g_subagent_approval_channel != NULL) {
        if (g_subagent_approval_channel->request_fd >= 0) {
            close(g_subagent_approval_channel->request_fd);
        }
        if (g_subagent_approval_channel->response_fd >= 0) {
            close(g_subagent_approval_channel->response_fd);
        }
        free(g_subagent_approval_channel);
        g_subagent_approval_channel = NULL;
    }
}

/** Returns NULL if not running as a subagent process. */
ApprovalChannel* subagent_get_approval_channel(void) {
    return g_subagent_approval_channel;
}

/** Initialize with explicit max_subagents and timeout. Values are clamped to safe ranges. */
int subagent_manager_init_with_config(SubagentManager *manager, int max_subagents, int timeout_seconds) {
    if (manager == NULL) {
        return -1;
    }

    if (max_subagents < 1) {
        max_subagents = SUBAGENT_MAX_DEFAULT;
    }
    if (max_subagents > SUBAGENT_HARD_CAP) {
        max_subagents = SUBAGENT_HARD_CAP;
    }
    if (timeout_seconds < 1) {
        timeout_seconds = SUBAGENT_TIMEOUT_DEFAULT;
    }
    if (timeout_seconds > SUBAGENT_MAX_TIMEOUT_SEC) {
        timeout_seconds = SUBAGENT_MAX_TIMEOUT_SEC;
    }

    if (SubagentArray_init(&manager->subagents) != 0) {
        return -1;
    }
    manager->max_subagents = max_subagents;
    manager->timeout_seconds = timeout_seconds;
    manager->is_subagent_process = 0;
    manager->gate_config = NULL;
    manager->spawn_callback = NULL;
    manager->spawn_callback_data = NULL;

    return 0;
}

void subagent_manager_set_gate_config(SubagentManager *manager, ApprovalGateConfig *gate_config) {
    if (manager != NULL) {
        manager->gate_config = gate_config;
    }
}

void subagent_manager_set_spawn_callback(SubagentManager *manager,
                                         SubagentSpawnCallback callback,
                                         void *user_data) {
    if (manager != NULL) {
        manager->spawn_callback = callback;
        manager->spawn_callback_data = user_data;
    }
}

/**
 * Clean up all subagent resources.
 * Kills any running subagents and frees all allocated memory.
 */
void subagent_manager_cleanup(SubagentManager *manager) {
    if (manager == NULL) {
        return;
    }

    for (size_t i = 0; i < manager->subagents.count; i++) {
        Subagent *sub = &manager->subagents.data[i];

        if (sub->status == SUBAGENT_STATUS_RUNNING && sub->pid > 0) {
            /* SIGTERM for graceful shutdown, SIGKILL if unresponsive */
            kill(sub->pid, SIGTERM);

            int status;
            pid_t result = waitpid(sub->pid, &status, WNOHANG);
            if (result == 0) {
                usleep(SUBAGENT_GRACE_PERIOD_USEC);
                kill(sub->pid, SIGKILL);
                waitpid(sub->pid, &status, 0);
            }
        }

        cleanup_subagent(sub);
    }

    SubagentArray_destroy(&manager->subagents);
}

Subagent* subagent_find_by_id(SubagentManager *manager, const char *subagent_id) {
    if (manager == NULL || subagent_id == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < manager->subagents.count; i++) {
        if (strcmp(manager->subagents.data[i].id, subagent_id) == 0) {
            return &manager->subagents.data[i];
        }
    }

    return NULL;
}

/**
 * Poll all running subagents for status changes (non-blocking).
 * Returns the number of subagents that changed state.
 */
int subagent_poll_all(SubagentManager *manager) {
    if (manager == NULL) {
        return -1;
    }

    int changed = 0;
    time_t now = time(NULL);

    for (size_t i = 0; i < manager->subagents.count; i++) {
        Subagent *sub = &manager->subagents.data[i];

        if (sub->status != SUBAGENT_STATUS_RUNNING) {
            continue;
        }

        if (now - sub->start_time > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            read_subagent_output(sub);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent execution timed out");
            subagent_notify_parent(sub);
            changed++;
            continue;
        }

        read_subagent_output_nonblocking(sub);

        int proc_status;
        pid_t result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (result == sub->pid) {
            subagent_handle_process_exit(sub, proc_status);
            subagent_notify_parent(sub);
            changed++;
        } else if (result == -1 && errno != ECHILD) {
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            subagent_notify_parent(sub);
            changed++;
        }
        // result == 0 means still running, no change
    }

    return changed;
}

/**
 * Spawn a new subagent to execute a task.
 * Forks a new process running ralph in subagent mode.
 * Creates approval channel pipes for IPC-based approval proxying.
 */
int subagent_spawn(SubagentManager *manager, const char *task, const char *context, char *subagent_id_out) {
    if (manager == NULL || task == NULL || subagent_id_out == NULL) {
        return -1;
    }

    if (manager->is_subagent_process) {
        return -1;
    }

    if (manager->max_subagents <= 0 || manager->subagents.count >= (size_t)manager->max_subagents) {
        return -1;
    }

    char id[SUBAGENT_ID_LENGTH + 1];
    generate_subagent_id(id);

    int stdout_pipefd[2];
    if (pipe(stdout_pipefd) == -1) {
        return -1;
    }

    int request_pipe[2], response_pipe[2];
    if (create_approval_channel_pipes(request_pipe, response_pipe) < 0) {
        close(stdout_pipefd[0]);
        close(stdout_pipefd[1]);
        return -1;
    }

    char *ralph_path = subagent_get_executable_path();
    if (ralph_path == NULL) {
        close(stdout_pipefd[0]);
        close(stdout_pipefd[1]);
        cleanup_approval_channel_pipes(request_pipe, response_pipe);
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        free(ralph_path);
        close(stdout_pipefd[0]);
        close(stdout_pipefd[1]);
        cleanup_approval_channel_pipes(request_pipe, response_pipe);
        return -1;
    }

    if (pid == 0) {
        close(stdout_pipefd[0]);

        if (dup2(stdout_pipefd[1], STDOUT_FILENO) == -1) {
            _exit(127);
        }
        close(stdout_pipefd[1]);

        if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
            _exit(127);
        }

        close(request_pipe[0]);
        close(response_pipe[1]);

        char request_fd_str[32], response_fd_str[32];
        snprintf(request_fd_str, sizeof(request_fd_str), "%d", request_pipe[1]);
        snprintf(response_fd_str, sizeof(response_fd_str), "%d", response_pipe[0]);
        setenv(RALPH_APPROVAL_REQUEST_FD, request_fd_str, 1);
        setenv(RALPH_APPROVAL_RESPONSE_FD, response_fd_str, 1);

        char* parent_id = messaging_tool_get_agent_id();
        if (parent_id != NULL) {
            setenv(RALPH_PARENT_AGENT_ID_ENV, parent_id, 1);
            free(parent_id);
        }

        char *args[7];
        int arg_idx = 0;

        args[arg_idx++] = ralph_path;
        args[arg_idx++] = "--subagent";
        args[arg_idx++] = "--task";
        args[arg_idx++] = (char*)task;

        if (context != NULL && strlen(context) > 0) {
            args[arg_idx++] = "--context";
            args[arg_idx++] = (char*)context;
        }

        args[arg_idx] = NULL;

        execv(ralph_path, args);
        _exit(127);
    }

    free(ralph_path);
    close(stdout_pipefd[1]);

    close(request_pipe[1]);
    close(response_pipe[0]);

    Subagent new_sub;
    memset(&new_sub, 0, sizeof(Subagent));

    /* Initialize FDs to -1 to prevent accidental close of stdin/stdout/stderr */
    new_sub.stdout_pipe[0] = -1;
    new_sub.stdout_pipe[1] = -1;
    new_sub.approval_channel.request_fd = -1;
    new_sub.approval_channel.response_fd = -1;

    memcpy(new_sub.id, id, SUBAGENT_ID_LENGTH);
    new_sub.id[SUBAGENT_ID_LENGTH] = '\0';
    new_sub.pid = pid;
    new_sub.status = SUBAGENT_STATUS_RUNNING;
    new_sub.stdout_pipe[0] = stdout_pipefd[0];
    new_sub.stdout_pipe[1] = -1;
    new_sub.approval_channel.request_fd = request_pipe[0];
    new_sub.approval_channel.response_fd = response_pipe[1];
    new_sub.approval_channel.subagent_pid = pid;
    new_sub.task = strdup(task);
    new_sub.context = (context != NULL && strlen(context) > 0) ? strdup(context) : NULL;
    new_sub.output = NULL;
    new_sub.output_len = 0;
    new_sub.result = NULL;
    new_sub.error = NULL;
    new_sub.start_time = time(NULL);

    if (new_sub.task == NULL || (context != NULL && strlen(context) > 0 && new_sub.context == NULL)) {
        if (new_sub.task) free(new_sub.task);
        if (new_sub.context) free(new_sub.context);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(stdout_pipefd[0]);
        close(request_pipe[0]);
        close(response_pipe[1]);
        return -1;
    }

    if (SubagentArray_push(&manager->subagents, new_sub) != 0) {
        if (new_sub.task) free(new_sub.task);
        if (new_sub.context) free(new_sub.context);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(stdout_pipefd[0]);
        close(request_pipe[0]);
        close(response_pipe[1]);
        return -1;
    }

    strcpy(subagent_id_out, id);

    /* Notify the main thread (if callback registered) that a new subagent was spawned.
     * This wakes up the main thread's select() loop so it can rebuild its
     * fd_set to include the new subagent's approval channel FD. Without this,
     * approval prompts would be delayed until the select() timeout or user input. */
    if (manager->spawn_callback != NULL) {
        manager->spawn_callback(manager->spawn_callback_data);
        debug_printf("subagent_spawn: Notified main thread of new subagent\n");
    }

    return 0;
}

/**
 * Get the current status of a subagent.
 * Optionally waits for the subagent to complete.
 *
 * @param manager Pointer to SubagentManager
 * @param subagent_id ID of the subagent to query
 * @param wait If non-zero, block until subagent completes (with timeout)
 * @param status Output: current status of the subagent
 * @param result Output: result string if completed (caller must free), NULL otherwise
 * @param error Output: error string if failed (caller must free), NULL otherwise
 * @return 0 on success, -1 if subagent not found or error
 */
int subagent_get_status(SubagentManager *manager, const char *subagent_id, int wait,
                        SubagentStatus *status, char **result, char **error) {
    if (manager == NULL || subagent_id == NULL || status == NULL) {
        return -1;
    }

    if (result != NULL) {
        *result = NULL;
    }
    if (error != NULL) {
        *error = NULL;
    }

    Subagent *sub = subagent_find_by_id(manager, subagent_id);
    if (sub == NULL) {
        *status = SUBAGENT_STATUS_FAILED;
        if (error != NULL) {
            *error = strdup("Subagent not found");
        }
        return -1;
    }

    #define RETURN_CURRENT_STATE() do { \
        *status = sub->status; \
        if (result != NULL && sub->result != NULL) { \
            *result = strdup(sub->result); \
        } \
        if (error != NULL && sub->error != NULL) { \
            *error = strdup(sub->error); \
        } \
        return 0; \
    } while (0)

    if (sub->status == SUBAGENT_STATUS_COMPLETED ||
        sub->status == SUBAGENT_STATUS_FAILED ||
        sub->status == SUBAGENT_STATUS_TIMEOUT) {
        RETURN_CURRENT_STATE();
    }

    time_t now = time(NULL);

    if (!wait) {
        if (now - sub->start_time > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            read_subagent_output(sub);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent execution timed out");
            subagent_notify_parent(sub);
            RETURN_CURRENT_STATE();
        }

        read_subagent_output_nonblocking(sub);

        int proc_status;
        pid_t waitpid_result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (waitpid_result == sub->pid) {
            subagent_handle_process_exit(sub, proc_status);
            subagent_notify_parent(sub);
        } else if (waitpid_result == -1 && errno != ECHILD) {
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            subagent_notify_parent(sub);
        }
        RETURN_CURRENT_STATE();
    }

    while (sub->status == SUBAGENT_STATUS_RUNNING) {
        if (interrupt_pending()) {
            interrupt_acknowledge();
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Interrupted by user");
            kill(sub->pid, SIGTERM);

            // Wait with timeout, escalate to SIGKILL if needed
            int proc_status;
            pid_t result = waitpid(sub->pid, &proc_status, WNOHANG);
            if (result == 0) {
                usleep(SUBAGENT_GRACE_PERIOD_USEC);
                kill(sub->pid, SIGKILL);
                waitpid(sub->pid, &proc_status, 0);
            }

            read_subagent_output(sub);
            subagent_notify_parent(sub);
            break;
        }

        now = time(NULL);

        if (now - sub->start_time > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            read_subagent_output(sub);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent execution timed out");
            subagent_notify_parent(sub);
            break;
        }

        /* Handle any pending approval requests from this subagent */
        if (manager->gate_config != NULL &&
            sub->approval_channel.request_fd > 2) {
            int idx = subagent_poll_approval_requests(manager, 0);
            if (idx >= 0) {
                subagent_handle_approval_request(manager, idx, manager->gate_config);
            }
        }

        read_subagent_output_nonblocking(sub);

        int proc_status;
        pid_t waitpid_result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (waitpid_result == sub->pid) {
            subagent_handle_process_exit(sub, proc_status);
            subagent_notify_parent(sub);
            break;
        } else if (waitpid_result == -1 && errno != ECHILD) {
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            subagent_notify_parent(sub);
            break;
        }

        usleep(SUBAGENT_POLL_INTERVAL_USEC);
    }

    #undef RETURN_CURRENT_STATE

    *status = sub->status;
    if (result != NULL && sub->result != NULL) {
        *result = strdup(sub->result);
    }
    if (error != NULL && sub->error != NULL) {
        *error = strdup(sub->error);
    }

    return 0;
}

/**
 * Extract a string value from JSON by key using cJSON.
 * Returns a newly allocated string that must be freed by the caller,
 * or NULL if the key is not found or is not a string.
 */
static char* extract_json_string_value(const char *json, const char *key) {
    if (json == NULL || key == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    char *result = NULL;

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        result = strdup(item->valuestring);
    }

    cJSON_Delete(root);
    return result;
}

/**
 * Extract a boolean value from JSON by key using cJSON.
 * Returns the default value if key is not found or is not a boolean.
 */
static int extract_json_boolean_value(const char *json, const char *key, int default_value) {
    if (json == NULL || key == NULL) {
        return default_value;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return default_value;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    int result = default_value;

    if (cJSON_IsBool(item)) {
        result = cJSON_IsTrue(item) ? 1 : 0;
    }

    cJSON_Delete(root);
    return result;
}

/**
 * Register the subagent tool with the tool registry.
 * Parameters:
 *   - task (required, string): Task description for the subagent to execute
 *   - context (optional, string): Additional context information
 */
int register_subagent_tool(ToolRegistry *registry, SubagentManager *manager) {
    if (registry == NULL || manager == NULL) {
        return -1;
    }

    if (g_subagent_manager != NULL && g_subagent_manager != manager) {
        fprintf(stderr, "Warning: Overwriting existing subagent manager pointer. "
                        "Only one SubagentManager should be active per process.\n");
    }

    g_subagent_manager = manager;

    ToolParameter parameters[2];
    memset(parameters, 0, sizeof(parameters));

    parameters[0].name = strdup("task");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("Task description for the subagent to execute");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;

    parameters[1].name = strdup("context");
    parameters[1].type = strdup("string");
    parameters[1].description = strdup("Optional context information to provide to the subagent");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;

    for (int i = 0; i < 2; i++) {
        if (parameters[i].name == NULL ||
            parameters[i].type == NULL ||
            parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(parameters[j].name);
                free(parameters[j].type);
                free(parameters[j].description);
            }
            return -1;
        }
    }

    int result = register_tool(registry, "subagent",
                              "Spawn a background subagent process to execute a delegated task. "
                              "The subagent runs with fresh context and cannot spawn additional subagents. "
                              "Results are automatically sent to you when the subagent completes - "
                              "no need to poll or wait for messages.",
                              parameters, 2, execute_subagent_tool_call);

    for (int i = 0; i < 2; i++) {
        free(parameters[i].name);
        free(parameters[i].type);
        free(parameters[i].description);
    }

    return result;
}

/**
 * Register the subagent_status tool with the tool registry.
 * Parameters:
 *   - subagent_id (required, string): ID of the subagent to query
 *   - wait (optional, boolean): Whether to block until completion (default: false)
 */
int register_subagent_status_tool(ToolRegistry *registry, SubagentManager *manager) {
    if (registry == NULL || manager == NULL) {
        return -1;
    }

    if (g_subagent_manager != NULL && g_subagent_manager != manager) {
        fprintf(stderr, "Warning: Overwriting existing subagent manager pointer. "
                        "Only one SubagentManager should be active per process.\n");
    }

    g_subagent_manager = manager;

    ToolParameter parameters[2];
    memset(parameters, 0, sizeof(parameters));

    parameters[0].name = strdup("subagent_id");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("ID of the subagent to query status for");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;

    parameters[1].name = strdup("wait");
    parameters[1].type = strdup("boolean");
    parameters[1].description = strdup("If true, block until the subagent completes (default: false)");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;

    for (int i = 0; i < 2; i++) {
        if (parameters[i].name == NULL ||
            parameters[i].type == NULL ||
            parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(parameters[j].name);
                free(parameters[j].type);
                free(parameters[j].description);
            }
            return -1;
        }
    }

    int result = register_tool(registry, "subagent_status",
                              "Query the status of a running or completed subagent. "
                              "Returns status (running/completed/failed/timeout), progress, result, and any errors. "
                              "Prefer waiting for messages from the subagent instead of polling this tool repeatedly.",
                              parameters, 2, execute_subagent_status_tool_call);

    for (int i = 0; i < 2; i++) {
        free(parameters[i].name);
        free(parameters[i].type);
        free(parameters[i].description);
    }

    return result;
}

/**
 * Execute the subagent tool call.
 * Spawns a new subagent process to handle the given task.
 */
int execute_subagent_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }

    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }

    if (g_subagent_manager == NULL) {
        result->result = strdup("{\"error\": \"Subagent manager not initialized\"}");
        result->success = 0;
        return 0;
    }

    if (g_subagent_manager->is_subagent_process) {
        result->result = strdup("{\"error\": \"Subagents cannot spawn additional subagents\"}");
        result->success = 0;
        return 0;
    }

    char *task = extract_json_string_value(tool_call->arguments, "task");
    if (task == NULL || strlen(task) == 0) {
        free(task);
        result->result = strdup("{\"error\": \"Task parameter is required\"}");
        result->success = 0;
        return 0;
    }

    char *context = extract_json_string_value(tool_call->arguments, "context");

    char subagent_id[SUBAGENT_ID_LENGTH + 1];
    int spawn_result = subagent_spawn(g_subagent_manager, task, context, subagent_id);

    free(task);
    free(context);

    if (spawn_result != 0) {
        if ((int)g_subagent_manager->subagents.count >= g_subagent_manager->max_subagents) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                     "{\"error\": \"Maximum number of concurrent subagents (%d) reached\"}",
                     g_subagent_manager->max_subagents);
            result->result = strdup(error_msg);
        } else {
            result->result = strdup("{\"error\": \"Failed to spawn subagent\"}");
        }
        result->success = 0;
        return 0;
    }

    char response[512];
    snprintf(response, sizeof(response),
             "{\"subagent_id\": \"%s\", \"status\": \"running\", \"message\": \"Subagent spawned successfully\"}",
             subagent_id);

    result->result = strdup(response);
    result->success = 1;

    return 0;
}

/**
 * Execute the subagent_status tool call.
 * Queries the status of an existing subagent.
 */
int execute_subagent_status_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }

    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }

    if (g_subagent_manager == NULL) {
        result->result = strdup("{\"error\": \"Subagent manager not initialized\"}");
        result->success = 0;
        return 0;
    }

    char *subagent_id = extract_json_string_value(tool_call->arguments, "subagent_id");
    if (subagent_id == NULL || strlen(subagent_id) == 0) {
        free(subagent_id);
        result->result = strdup("{\"error\": \"subagent_id parameter is required\"}");
        result->success = 0;
        return 0;
    }

    int wait = extract_json_boolean_value(tool_call->arguments, "wait", 0);

    SubagentStatus status;
    char *subagent_result = NULL;
    char *error = NULL;

    int get_result = subagent_get_status(g_subagent_manager, subagent_id, wait,
                                         &status, &subagent_result, &error);

    free(subagent_id);

    if (get_result != 0) {
        char *escaped_error = json_escape_string(error ? error : "Subagent not found");
        char response[512];
        snprintf(response, sizeof(response),
                 "{\"error\": \"%s\"}",
                 escaped_error ? escaped_error : "Unknown error");
        free(escaped_error);
        free(error);
        result->result = strdup(response);
        result->success = 0;
        return 0;
    }

    const char *status_str = subagent_status_to_string(status);
    size_t response_size = 256;
    char *escaped_result = NULL;
    char *escaped_error = NULL;

    if (subagent_result != NULL) {
        escaped_result = json_escape_string(subagent_result);
        if (escaped_result != NULL) {
            response_size += strlen(escaped_result);
        }
    }
    if (error != NULL) {
        escaped_error = json_escape_string(error);
        if (escaped_error != NULL) {
            response_size += strlen(escaped_error);
        }
    }

    char *response = malloc(response_size);
    if (response == NULL) {
        free(subagent_result);
        free(error);
        free(escaped_result);
        free(escaped_error);
        result->result = strdup("{\"error\": \"Memory allocation failed\"}");
        result->success = 0;
        return 0;
    }

    if (status == SUBAGENT_STATUS_COMPLETED && escaped_result != NULL) {
        snprintf(response, response_size,
                 "{\"status\": \"%s\", \"result\": \"%s\"}",
                 status_str, escaped_result);
        result->success = 1;
    } else if ((status == SUBAGENT_STATUS_FAILED || status == SUBAGENT_STATUS_TIMEOUT) && escaped_error != NULL) {
        snprintf(response, response_size,
                 "{\"status\": \"%s\", \"error\": \"%s\"}",
                 status_str, escaped_error);
        result->success = 0;
    } else if (status == SUBAGENT_STATUS_RUNNING) {
        snprintf(response, response_size,
                 "{\"status\": \"%s\", \"message\": \"Subagent is still running\"}",
                 status_str);
        result->success = 1;
    } else {
        snprintf(response, response_size,
                 "{\"status\": \"%s\"}",
                 status_str);
        result->success = (status == SUBAGENT_STATUS_COMPLETED) ? 1 : 0;
    }

    result->result = response;

    free(subagent_result);
    free(error);
    free(escaped_result);
    free(escaped_error);

    return 0;
}

/**
 * Poll all running subagents for pending approval requests.
 * Returns the index of the first subagent with a pending request.
 *
 * @param manager Pointer to SubagentManager
 * @param timeout_ms Maximum time to wait in milliseconds (0 for non-blocking)
 * @return Index of subagent with pending request, or -1 if none
 */
int subagent_poll_approval_requests(SubagentManager *manager, int timeout_ms) {
    if (manager == NULL) {
        return -1;
    }

    int running_count = 0;
    for (size_t i = 0; i < manager->subagents.count; i++) {
        Subagent *sub = &manager->subagents.data[i];
        if (sub->status == SUBAGENT_STATUS_RUNNING &&
            sub->approval_channel.request_fd >= 0) {
            running_count++;
        }
    }

    if (running_count == 0) {
        return -1;
    }

    struct pollfd *pfds = malloc(running_count * sizeof(struct pollfd));
    int *indices = malloc(running_count * sizeof(int));
    if (pfds == NULL || indices == NULL) {
        free(pfds);
        free(indices);
        return -1;
    }

    int poll_idx = 0;
    for (size_t i = 0; i < manager->subagents.count; i++) {
        Subagent *sub = &manager->subagents.data[i];
        if (sub->status == SUBAGENT_STATUS_RUNNING &&
            sub->approval_channel.request_fd > 2) {  // > 2 to skip stdin/stdout/stderr
            pfds[poll_idx].fd = sub->approval_channel.request_fd;
            pfds[poll_idx].events = POLLIN;
            pfds[poll_idx].revents = 0;
            indices[poll_idx] = (int)i;
            poll_idx++;
        }
    }

    int ready = poll(pfds, poll_idx, timeout_ms);
    if (ready <= 0) {
        free(pfds);
        free(indices);
        return -1;
    }

    int result = -1;
    for (int i = 0; i < poll_idx; i++) {
        if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
            result = indices[i];
            break;
        }
    }

    free(pfds);
    free(indices);
    return result;
}

/**
 * Handle an approval request from a specific subagent.
 * Prompts the user via TTY and sends the response back to the subagent.
 *
 * @param manager Pointer to SubagentManager
 * @param subagent_index Index of the subagent in the manager's array
 * @param gate_config Parent's approval gate configuration
 * @return 0 on success, -1 on error
 */
int subagent_handle_approval_request(SubagentManager *manager,
                                     int subagent_index,
                                     ApprovalGateConfig *gate_config) {
    if (manager == NULL || gate_config == NULL) {
        return -1;
    }

    if (subagent_index < 0 || (size_t)subagent_index >= manager->subagents.count) {
        return -1;
    }

    Subagent *sub = &manager->subagents.data[subagent_index];
    if (sub->status != SUBAGENT_STATUS_RUNNING) {
        return -1;
    }

    int result = handle_subagent_approval_request(gate_config, &sub->approval_channel, sub->id);

    if (result < 0) {
        /* Pipe broken or closed - close the approval channel FD to prevent
         * further polling. The subagent may have exited or crashed. */
        if (sub->approval_channel.request_fd > 2) {
            close(sub->approval_channel.request_fd);
            sub->approval_channel.request_fd = -1;
        }
        if (sub->approval_channel.response_fd > 2) {
            close(sub->approval_channel.response_fd);
            sub->approval_channel.response_fd = -1;
        }
        return -1;
    }

    return 0;
}

/**
 * Check and handle any pending approval requests from subagents.
 * Non-blocking check that handles at most one request.
 *
 * @param manager Pointer to SubagentManager
 * @param gate_config Parent's approval gate configuration
 * @return 1 if a request was handled, 0 if none pending, -1 on error
 */
int subagent_check_and_handle_approvals(SubagentManager *manager,
                                        ApprovalGateConfig *gate_config) {
    if (manager == NULL || gate_config == NULL) {
        return -1;
    }

    int idx = subagent_poll_approval_requests(manager, 0);
    if (idx < 0) {
        return 0;
    }

    if (subagent_handle_approval_request(manager, idx, gate_config) == 0) {
        return 1;
    }

    return -1;
}
