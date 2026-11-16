#include "subagent_tool.h"
#include "subagent_process.h"
#include "../util/json_escape.h"
#include "../util/interrupt.h"
#include "../util/config.h"
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
    manager->services = NULL;

    return 0;
}

void subagent_manager_set_services(SubagentManager *manager, Services *services) {
    if (manager != NULL) {
        manager->services = services;
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

        cleanup_subagent(sub, manager->services);
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
            subagent_notify_parent(sub, manager->services);
            changed++;
            continue;
        }

        read_subagent_output_nonblocking(sub);

        int proc_status;
        pid_t result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (result == sub->pid) {
            subagent_handle_process_exit(sub, proc_status);
            subagent_notify_parent(sub, manager->services);
            changed++;
        } else if (result == -1 && errno != ECHILD) {
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            subagent_notify_parent(sub, manager->services);
            changed++;
        }
        // result == 0 means still running, no change
    }

    return changed;
}

/* Set up child process FDs and environment for subagent IPC.
 * Called in the child (post-fork). Does not return on success. */
static void child_setup_ipc(int stdout_write_fd) {
    if (dup2(stdout_write_fd, STDOUT_FILENO) == -1) _exit(127);
    close(stdout_write_fd);
    if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) _exit(127);

    char* parent_id = messaging_tool_get_agent_id();
    if (parent_id != NULL) {
        setenv(RALPH_PARENT_AGENT_ID_ENV, parent_id, 1);
        free(parent_id);
    }
}

/* Register a freshly-forked child as a tracked subagent.
 * Closes parent-side write ends, builds Subagent struct, pushes to manager.
 * On failure kills the child and closes all FDs. */
static int register_subagent(SubagentManager *manager, const char id[SUBAGENT_ID_LENGTH + 1],
                              pid_t pid, int stdout_read_fd,
                              const char *task, const char *context_str,
                              char *subagent_id_out) {
    Subagent new_sub;
    memset(&new_sub, 0, sizeof(Subagent));

    new_sub.stdout_pipe[0] = -1;
    new_sub.stdout_pipe[1] = -1;

    memcpy(new_sub.id, id, SUBAGENT_ID_LENGTH);
    new_sub.id[SUBAGENT_ID_LENGTH] = '\0';
    new_sub.pid = pid;
    new_sub.status = SUBAGENT_STATUS_RUNNING;
    new_sub.stdout_pipe[0] = stdout_read_fd;
    new_sub.stdout_pipe[1] = -1;
    new_sub.task = (task != NULL) ? strdup(task) : strdup("subagent");
    new_sub.context = (context_str != NULL && strlen(context_str) > 0)
                      ? strdup(context_str) : NULL;
    new_sub.output = NULL;
    new_sub.output_len = 0;
    new_sub.result = NULL;
    new_sub.error = NULL;
    new_sub.start_time = time(NULL);

    if (new_sub.task == NULL ||
        (context_str != NULL && strlen(context_str) > 0 && new_sub.context == NULL)) {
        free(new_sub.task);
        free(new_sub.context);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(stdout_read_fd);
        return -1;
    }

    if (SubagentArray_push(&manager->subagents, new_sub) != 0) {
        free(new_sub.task);
        free(new_sub.context);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(stdout_read_fd);
        return -1;
    }

    strcpy(subagent_id_out, id);

    return 0;
}

/* Common pre-fork validation and pipe creation. Returns 0 on success. */
static int prefork_setup(SubagentManager *manager, char id_out[SUBAGENT_ID_LENGTH + 1],
                          int stdout_pipefd[2]) {
    if (manager->is_subagent_process) return -1;
    if (manager->max_subagents <= 0 ||
        manager->subagents.count >= (size_t)manager->max_subagents) return -1;

    generate_subagent_id(id_out);

    if (pipe(stdout_pipefd) == -1) return -1;

    return 0;
}

/**
 * Spawn a new subagent to execute a task.
 * Forks a new process running ralph in subagent mode.
 */
int subagent_spawn(SubagentManager *manager, const char *task, const char *context, const char *model, char *subagent_id_out) {
    if (manager == NULL || task == NULL || subagent_id_out == NULL) return -1;

    char id[SUBAGENT_ID_LENGTH + 1];
    int stdout_pipefd[2];
    if (prefork_setup(manager, id, stdout_pipefd) != 0)
        return -1;

    char *ralph_path = subagent_get_executable_path();
    if (ralph_path == NULL) {
        close(stdout_pipefd[0]); close(stdout_pipefd[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        free(ralph_path);
        close(stdout_pipefd[0]); close(stdout_pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(stdout_pipefd[0]);
        child_setup_ipc(stdout_pipefd[1]);

        char *args[9];
        int ai = 0;
        args[ai++] = ralph_path;
        args[ai++] = "--subagent";
        args[ai++] = "--task";
        args[ai++] = (char*)task;
        if (context != NULL && strlen(context) > 0) {
            args[ai++] = "--context";
            args[ai++] = (char*)context;
        }
        if (model != NULL && strlen(model) > 0) {
            args[ai++] = "--model";
            args[ai++] = (char*)model;
        }
        args[ai] = NULL;

        execv(ralph_path, args);
        _exit(127);
    }

    free(ralph_path);
    close(stdout_pipefd[1]);

    return register_subagent(manager, id, pid, stdout_pipefd[0],
                             task, context, subagent_id_out);
}

/**
 * Spawn a subagent with a custom argv array.
 * Same pipe setup as subagent_spawn, but the child
 * calls execv(args[0], args) with the caller-provided argument list.
 * task_desc populates the Subagent.task field for completion notifications.
 */
int subagent_spawn_with_args(SubagentManager *manager, char *args[],
                             const char *task_desc, char *subagent_id_out) {
    if (manager == NULL || args == NULL || args[0] == NULL || subagent_id_out == NULL)
        return -1;

    char id[SUBAGENT_ID_LENGTH + 1];
    int stdout_pipefd[2];
    if (prefork_setup(manager, id, stdout_pipefd) != 0)
        return -1;

    pid_t pid = fork();
    if (pid == -1) {
        close(stdout_pipefd[0]); close(stdout_pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(stdout_pipefd[0]);
        child_setup_ipc(stdout_pipefd[1]);
        execv(args[0], args);
        _exit(127);
    }

    close(stdout_pipefd[1]);

    return register_subagent(manager, id, pid, stdout_pipefd[0],
                             task_desc, NULL, subagent_id_out);
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
            subagent_notify_parent(sub, manager->services);
            RETURN_CURRENT_STATE();
        }

        read_subagent_output_nonblocking(sub);

        int proc_status;
        pid_t waitpid_result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (waitpid_result == sub->pid) {
            subagent_handle_process_exit(sub, proc_status);
            subagent_notify_parent(sub, manager->services);
        } else if (waitpid_result == -1 && errno != ECHILD) {
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            subagent_notify_parent(sub, manager->services);
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
            subagent_notify_parent(sub, manager->services);
            break;
        }

        now = time(NULL);

        if (now - sub->start_time > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            read_subagent_output(sub);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent execution timed out");
            subagent_notify_parent(sub, manager->services);
            break;
        }

        read_subagent_output_nonblocking(sub);

        int proc_status;
        pid_t waitpid_result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (waitpid_result == sub->pid) {
            subagent_handle_process_exit(sub, proc_status);
            subagent_notify_parent(sub, manager->services);
            break;
        } else if (waitpid_result == -1 && errno != ECHILD) {
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            subagent_notify_parent(sub, manager->services);
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
    manager->services = registry->services;

    ToolParameter parameters[3];
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

    parameters[2].name = strdup("model");
    parameters[2].type = strdup("string");
    parameters[2].description = strdup("Model to use for this subagent (e.g. 'simple', 'standard', 'high', or a model ID). Omit to inherit the current model.");
    parameters[2].enum_values = NULL;
    parameters[2].enum_count = 0;
    parameters[2].required = 0;

    for (int i = 0; i < 3; i++) {
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
                              "Spawn a background subagent to execute a delegated task independently. "
                              "The subagent runs with fresh context and its own tool access (except spawning further subagents). "
                              "Results are delivered as a message when complete — do not poll. "
                              "Use for parallelizable work like searching code, running tests, or independent subtasks. "
                              "Keep task descriptions self-contained since the subagent has no access to your conversation.",
                              parameters, 3, execute_subagent_tool_call);

    for (int i = 0; i < 3; i++) {
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
    manager->services = registry->services;

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
    char *model_raw = extract_json_string_value(tool_call->arguments, "model");

    const char *resolved_model = NULL;
    if (model_raw && strlen(model_raw) > 0) {
        resolved_model = config_resolve_model(model_raw);
    } else {
        resolved_model = config_get_string("model");
    }

    char subagent_id[SUBAGENT_ID_LENGTH + 1];
    int spawn_result = subagent_spawn(g_subagent_manager, task, context, resolved_model, subagent_id);

    free(task);
    free(context);
    free(model_raw);

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
