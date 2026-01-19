#include "subagent_tool.h"
#include "../utils/config.h"
#include "../core/ralph.h"
#include "../session/conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

/**
 * Generate a unique subagent ID using random hex characters.
 * Uses /dev/urandom for cryptographic randomness with a time/pid fallback.
 */
void generate_subagent_id(char *id_out) {
    static const char hex[] = "0123456789abcdef";
    unsigned char random_bytes[SUBAGENT_ID_LENGTH / 2];

    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t read_count = fread(random_bytes, 1, sizeof(random_bytes), f);
        fclose(f);
        if (read_count != sizeof(random_bytes)) {
            // Fallback if read was incomplete
            srand((unsigned int)(time(NULL) ^ getpid()));
            for (size_t i = read_count; i < sizeof(random_bytes); i++) {
                random_bytes[i] = (unsigned char)(rand() & 0xFF);
            }
        }
    } else {
        // Fallback: combine time and pid for randomness
        srand((unsigned int)(time(NULL) ^ getpid()));
        for (size_t i = 0; i < sizeof(random_bytes); i++) {
            random_bytes[i] = (unsigned char)(rand() & 0xFF);
        }
    }

    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        id_out[i * 2] = hex[random_bytes[i] >> 4];
        id_out[i * 2 + 1] = hex[random_bytes[i] & 0x0F];
    }
    id_out[SUBAGENT_ID_LENGTH] = '\0';
}

/**
 * Convert subagent status enum to human-readable string.
 */
const char* subagent_status_to_string(SubagentStatus status) {
    switch (status) {
        case SUBAGENT_STATUS_PENDING:
            return "pending";
        case SUBAGENT_STATUS_RUNNING:
            return "running";
        case SUBAGENT_STATUS_COMPLETED:
            return "completed";
        case SUBAGENT_STATUS_FAILED:
            return "failed";
        case SUBAGENT_STATUS_TIMEOUT:
            return "timeout";
        default:
            return "unknown";
    }
}

/**
 * Clean up resources for a single subagent.
 * Closes pipes and frees allocated strings.
 */
void cleanup_subagent(Subagent *sub) {
    if (sub == NULL) {
        return;
    }

    // Close pipe if still open
    if (sub->stdout_pipe[0] > 0) {
        close(sub->stdout_pipe[0]);
        sub->stdout_pipe[0] = -1;
    }

    // Free allocated strings
    if (sub->task) {
        free(sub->task);
        sub->task = NULL;
    }
    if (sub->context) {
        free(sub->context);
        sub->context = NULL;
    }
    if (sub->output) {
        free(sub->output);
        sub->output = NULL;
    }
    if (sub->result) {
        free(sub->result);
        sub->result = NULL;
    }
    if (sub->error) {
        free(sub->error);
        sub->error = NULL;
    }

    sub->output_len = 0;
    sub->pid = 0;
    sub->status = SUBAGENT_STATUS_PENDING;
}

/**
 * Initialize the subagent manager with default configuration.
 * Reads max_subagents and subagent_timeout from the global config.
 */
int subagent_manager_init(SubagentManager *manager) {
    if (manager == NULL) {
        return -1;
    }

    // Get configuration values
    ralph_config_t *config = config_get();
    int max_subagents = SUBAGENT_MAX_DEFAULT;
    int timeout_seconds = SUBAGENT_TIMEOUT_DEFAULT;

    if (config != NULL) {
        max_subagents = config->max_subagents > 0 ? config->max_subagents : SUBAGENT_MAX_DEFAULT;
        timeout_seconds = config->subagent_timeout > 0 ? config->subagent_timeout : SUBAGENT_TIMEOUT_DEFAULT;
    }

    return subagent_manager_init_with_config(manager, max_subagents, timeout_seconds);
}

/**
 * Initialize the subagent manager with specific configuration values.
 */
int subagent_manager_init_with_config(SubagentManager *manager, int max_subagents, int timeout_seconds) {
    if (manager == NULL) {
        return -1;
    }

    // Validate and clamp configuration values
    if (max_subagents < 1) {
        max_subagents = SUBAGENT_MAX_DEFAULT;
    }
    if (max_subagents > 20) {
        max_subagents = 20;  // Hard cap for safety
    }
    if (timeout_seconds < 1) {
        timeout_seconds = SUBAGENT_TIMEOUT_DEFAULT;
    }
    if (timeout_seconds > 3600) {
        timeout_seconds = 3600;  // Max 1 hour
    }

    manager->subagents = NULL;
    manager->count = 0;
    manager->max_subagents = max_subagents;
    manager->timeout_seconds = timeout_seconds;
    manager->is_subagent_process = 0;

    return 0;
}

/**
 * Clean up all subagent resources.
 * Kills any running subagents and frees all allocated memory.
 */
void subagent_manager_cleanup(SubagentManager *manager) {
    if (manager == NULL) {
        return;
    }

    // Kill and clean up all subagents
    for (int i = 0; i < manager->count; i++) {
        Subagent *sub = &manager->subagents[i];

        // Kill running processes
        if (sub->status == SUBAGENT_STATUS_RUNNING && sub->pid > 0) {
            // Send SIGTERM first for graceful shutdown
            kill(sub->pid, SIGTERM);

            // Give it a brief moment to exit
            int status;
            pid_t result = waitpid(sub->pid, &status, WNOHANG);
            if (result == 0) {
                // Still running, use SIGKILL
                usleep(100000);  // 100ms grace period
                kill(sub->pid, SIGKILL);
                waitpid(sub->pid, &status, 0);
            }
        }

        // Clean up subagent resources
        cleanup_subagent(sub);
    }

    // Free the subagents array
    if (manager->subagents) {
        free(manager->subagents);
        manager->subagents = NULL;
    }

    manager->count = 0;
}

/**
 * Find a subagent by its ID.
 */
Subagent* subagent_find_by_id(SubagentManager *manager, const char *subagent_id) {
    if (manager == NULL || subagent_id == NULL) {
        return NULL;
    }

    for (int i = 0; i < manager->count; i++) {
        if (strcmp(manager->subagents[i].id, subagent_id) == 0) {
            return &manager->subagents[i];
        }
    }

    return NULL;
}

/**
 * Read available output from a subagent's pipe (non-blocking).
 * Sets pipe to non-blocking mode and reads any available data.
 */
int read_subagent_output_nonblocking(Subagent *sub) {
    if (sub == NULL || sub->stdout_pipe[0] <= 0) {
        return -1;
    }

    // Set pipe to non-blocking
    int flags = fcntl(sub->stdout_pipe[0], F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    fcntl(sub->stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    char buffer[4096];
    int total_read = 0;

    while (1) {
        ssize_t bytes_read = read(sub->stdout_pipe[0], buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // No more data available
                break;
            }
            if (bytes_read == 0) {
                // EOF
                break;
            }
            // Error
            return -1;
        }

        buffer[bytes_read] = '\0';
        total_read += (int)bytes_read;

        // Append to output buffer (with limit check)
        size_t new_len = sub->output_len + (size_t)bytes_read;
        if (new_len > SUBAGENT_MAX_OUTPUT_LENGTH) {
            // Truncate to max length
            bytes_read = (ssize_t)(SUBAGENT_MAX_OUTPUT_LENGTH - sub->output_len);
            if (bytes_read <= 0) {
                break;
            }
            new_len = SUBAGENT_MAX_OUTPUT_LENGTH;
        }

        char *new_output = realloc(sub->output, new_len + 1);
        if (new_output == NULL) {
            return -1;
        }
        sub->output = new_output;
        memcpy(sub->output + sub->output_len, buffer, (size_t)bytes_read);
        sub->output_len = new_len;
        sub->output[sub->output_len] = '\0';
    }

    // Restore blocking mode
    fcntl(sub->stdout_pipe[0], F_SETFL, flags);

    return total_read;
}

/**
 * Read all remaining output from a subagent's pipe (blocking).
 * Called when subagent has exited to collect final output.
 */
int read_subagent_output(Subagent *sub) {
    if (sub == NULL || sub->stdout_pipe[0] <= 0) {
        return -1;
    }

    char buffer[4096];

    while (1) {
        ssize_t bytes_read = read(sub->stdout_pipe[0], buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                // EOF - done reading
                break;
            }
            // Error
            return -1;
        }

        buffer[bytes_read] = '\0';

        // Check output limit
        size_t new_len = sub->output_len + (size_t)bytes_read;
        if (new_len > SUBAGENT_MAX_OUTPUT_LENGTH) {
            bytes_read = (ssize_t)(SUBAGENT_MAX_OUTPUT_LENGTH - sub->output_len);
            if (bytes_read <= 0) {
                break;
            }
            new_len = SUBAGENT_MAX_OUTPUT_LENGTH;
        }

        // Append to output buffer
        char *new_output = realloc(sub->output, new_len + 1);
        if (new_output == NULL) {
            return -1;
        }
        sub->output = new_output;
        memcpy(sub->output + sub->output_len, buffer, (size_t)bytes_read);
        sub->output_len = new_len;
        sub->output[sub->output_len] = '\0';
    }

    // Close the pipe
    close(sub->stdout_pipe[0]);
    sub->stdout_pipe[0] = -1;

    return 0;
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

    for (int i = 0; i < manager->count; i++) {
        Subagent *sub = &manager->subagents[i];

        // Skip non-running subagents
        if (sub->status != SUBAGENT_STATUS_RUNNING) {
            continue;
        }

        // Check for timeout
        if (now - sub->start_time > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            read_subagent_output(sub);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent execution timed out");
            changed++;
            continue;
        }

        // Read any available output
        read_subagent_output_nonblocking(sub);

        // Check process status
        int proc_status;
        pid_t result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (result == sub->pid) {
            // Process has exited
            read_subagent_output(sub);

            if (WIFEXITED(proc_status) && WEXITSTATUS(proc_status) == 0) {
                sub->status = SUBAGENT_STATUS_COMPLETED;
                sub->result = sub->output;
                sub->output = NULL;
                sub->output_len = 0;
            } else {
                sub->status = SUBAGENT_STATUS_FAILED;
                if (WIFEXITED(proc_status)) {
                    char error_msg[64];
                    snprintf(error_msg, sizeof(error_msg), "Subagent exited with code %d", WEXITSTATUS(proc_status));
                    sub->error = strdup(error_msg);
                } else if (WIFSIGNALED(proc_status)) {
                    char error_msg[64];
                    snprintf(error_msg, sizeof(error_msg), "Subagent killed by signal %d", WTERMSIG(proc_status));
                    sub->error = strdup(error_msg);
                } else {
                    sub->error = strdup("Subagent process failed");
                }
            }
            changed++;
        } else if (result == -1 && errno != ECHILD) {
            // Error in waitpid
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            changed++;
        }
        // result == 0 means still running, no change
    }

    return changed;
}

/**
 * Get the path to the current executable.
 * Uses /proc/self/exe on Linux with fallback options.
 * Returns a newly allocated string that must be freed by caller.
 */
static char* get_executable_path(void) {
    char *path = malloc(4096);
    if (path == NULL) {
        return NULL;
    }

    // Try /proc/self/exe first (Linux)
    ssize_t len = readlink("/proc/self/exe", path, 4095);
    if (len > 0) {
        path[len] = '\0';
        return path;
    }

    // Fallback: try current directory
    if (getcwd(path, 4096) != NULL) {
        size_t cwd_len = strlen(path);
        if (cwd_len + 7 < 4096) {  // "/ralph" + null
            strcat(path, "/ralph");
            if (access(path, X_OK) == 0) {
                return path;
            }
        }
    }

    // Last fallback: assume ./ralph
    strcpy(path, "./ralph");
    free(path);
    return strdup("./ralph");
}

/**
 * Spawn a new subagent to execute a task.
 * Forks a new process running ralph in subagent mode.
 */
int subagent_spawn(SubagentManager *manager, const char *task, const char *context, char *subagent_id_out) {
    if (manager == NULL || task == NULL || subagent_id_out == NULL) {
        return -1;
    }

    // Prevent nesting: don't allow subagents to spawn subagents
    if (manager->is_subagent_process) {
        return -1;
    }

    // Check max limit
    if (manager->count >= manager->max_subagents) {
        return -1;
    }

    // Generate unique ID
    char id[SUBAGENT_ID_LENGTH + 1];
    generate_subagent_id(id);

    // Create pipe for capturing child output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }

    // Get path to ralph executable
    char *ralph_path = get_executable_path();
    if (ralph_path == NULL) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    // Fork the process
    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        free(ralph_path);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end

        // Redirect stdout to pipe
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            _exit(127);
        }
        close(pipefd[1]);

        // Also redirect stderr to stdout so we capture errors
        dup2(STDOUT_FILENO, STDERR_FILENO);

        // Build exec arguments
        // Maximum: ralph --subagent --task "..." --context "..." NULL = 7 args
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

        // Execute ralph in subagent mode
        execv(ralph_path, args);

        // If execv fails, exit with error
        _exit(127);
    }

    // Parent process
    free(ralph_path);
    close(pipefd[1]);  // Close write end

    // Expand the subagents array
    Subagent *new_subagents = realloc(manager->subagents,
                                       (size_t)(manager->count + 1) * sizeof(Subagent));
    if (new_subagents == NULL) {
        // Memory allocation failed - kill the child and clean up
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(pipefd[0]);
        return -1;
    }
    manager->subagents = new_subagents;

    // Initialize the new subagent entry
    Subagent *sub = &manager->subagents[manager->count];
    memset(sub, 0, sizeof(Subagent));

    memcpy(sub->id, id, SUBAGENT_ID_LENGTH);
    sub->id[SUBAGENT_ID_LENGTH] = '\0';
    sub->pid = pid;
    sub->status = SUBAGENT_STATUS_RUNNING;
    sub->stdout_pipe[0] = pipefd[0];
    sub->stdout_pipe[1] = -1;  // Write end closed in parent
    sub->task = strdup(task);
    sub->context = (context != NULL && strlen(context) > 0) ? strdup(context) : NULL;
    sub->output = NULL;
    sub->output_len = 0;
    sub->result = NULL;
    sub->error = NULL;
    sub->start_time = time(NULL);

    // Check if strdup failed
    if (sub->task == NULL || (context != NULL && strlen(context) > 0 && sub->context == NULL)) {
        // Cleanup on allocation failure
        if (sub->task) free(sub->task);
        if (sub->context) free(sub->context);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(pipefd[0]);
        return -1;
    }

    manager->count++;

    // Copy ID to output
    strcpy(subagent_id_out, id);

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

    // Initialize outputs
    if (result != NULL) {
        *result = NULL;
    }
    if (error != NULL) {
        *error = NULL;
    }

    // Find the subagent
    Subagent *sub = subagent_find_by_id(manager, subagent_id);
    if (sub == NULL) {
        *status = SUBAGENT_STATUS_FAILED;
        if (error != NULL) {
            *error = strdup("Subagent not found");
        }
        return -1;
    }

    // Helper macro to return current state
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

    // If already in a terminal state, return cached values
    if (sub->status == SUBAGENT_STATUS_COMPLETED ||
        sub->status == SUBAGENT_STATUS_FAILED ||
        sub->status == SUBAGENT_STATUS_TIMEOUT) {
        RETURN_CURRENT_STATE();
    }

    // Subagent is still running - need to check/wait
    time_t now = time(NULL);

    if (!wait) {
        // Non-blocking: check once and return

        // Check timeout first
        if (now - sub->start_time > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            read_subagent_output(sub);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent execution timed out");
            RETURN_CURRENT_STATE();
        }

        // Read any available output
        read_subagent_output_nonblocking(sub);

        // Check process status non-blocking
        int proc_status;
        pid_t waitpid_result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (waitpid_result == sub->pid) {
            // Process has exited
            read_subagent_output(sub);

            if (WIFEXITED(proc_status) && WEXITSTATUS(proc_status) == 0) {
                sub->status = SUBAGENT_STATUS_COMPLETED;
                sub->result = sub->output;
                sub->output = NULL;
                sub->output_len = 0;
            } else {
                sub->status = SUBAGENT_STATUS_FAILED;
                if (WIFEXITED(proc_status)) {
                    char error_msg[64];
                    snprintf(error_msg, sizeof(error_msg),
                             "Subagent exited with code %d", WEXITSTATUS(proc_status));
                    sub->error = strdup(error_msg);
                } else if (WIFSIGNALED(proc_status)) {
                    char error_msg[64];
                    snprintf(error_msg, sizeof(error_msg),
                             "Subagent killed by signal %d", WTERMSIG(proc_status));
                    sub->error = strdup(error_msg);
                } else {
                    sub->error = strdup("Subagent process failed");
                }
            }
        } else if (waitpid_result == -1 && errno != ECHILD) {
            // Error in waitpid
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
        }
        // waitpid_result == 0 means still running, status unchanged

        RETURN_CURRENT_STATE();
    }

    // Blocking wait: loop until completion or timeout
    while (sub->status == SUBAGENT_STATUS_RUNNING) {
        now = time(NULL);

        // Check timeout
        if (now - sub->start_time > manager->timeout_seconds) {
            kill(sub->pid, SIGKILL);
            waitpid(sub->pid, NULL, 0);
            read_subagent_output(sub);
            sub->status = SUBAGENT_STATUS_TIMEOUT;
            sub->error = strdup("Subagent execution timed out");
            break;
        }

        // Read any available output while waiting
        read_subagent_output_nonblocking(sub);

        // Wait for process to exit (with brief timeout for periodic checks)
        int proc_status;
        pid_t waitpid_result = waitpid(sub->pid, &proc_status, WNOHANG);

        if (waitpid_result == sub->pid) {
            // Process has exited
            read_subagent_output(sub);

            if (WIFEXITED(proc_status) && WEXITSTATUS(proc_status) == 0) {
                sub->status = SUBAGENT_STATUS_COMPLETED;
                sub->result = sub->output;
                sub->output = NULL;
                sub->output_len = 0;
            } else {
                sub->status = SUBAGENT_STATUS_FAILED;
                if (WIFEXITED(proc_status)) {
                    char error_msg[64];
                    snprintf(error_msg, sizeof(error_msg),
                             "Subagent exited with code %d", WEXITSTATUS(proc_status));
                    sub->error = strdup(error_msg);
                } else if (WIFSIGNALED(proc_status)) {
                    char error_msg[64];
                    snprintf(error_msg, sizeof(error_msg),
                             "Subagent killed by signal %d", WTERMSIG(proc_status));
                    sub->error = strdup(error_msg);
                } else {
                    sub->error = strdup("Subagent process failed");
                }
            }
            break;
        } else if (waitpid_result == -1 && errno != ECHILD) {
            // Error in waitpid
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            break;
        }

        // Still running, sleep briefly before checking again
        usleep(50000);  // 50ms polling interval
    }

    #undef RETURN_CURRENT_STATE

    // Return final state
    *status = sub->status;
    if (result != NULL && sub->result != NULL) {
        *result = strdup(sub->result);
    }
    if (error != NULL && sub->error != NULL) {
        *error = strdup(sub->error);
    }

    return 0;
}

int register_subagent_tool(ToolRegistry *registry, SubagentManager *manager) {
    (void)registry;
    (void)manager;
    // TODO: Implement in Step 8
    return -1;
}

int register_subagent_status_tool(ToolRegistry *registry, SubagentManager *manager) {
    (void)registry;
    (void)manager;
    // TODO: Implement in Step 8
    return -1;
}

int execute_subagent_tool_call(const ToolCall *tool_call, ToolResult *result) {
    (void)tool_call;
    (void)result;
    // TODO: Implement in Step 8
    return -1;
}

int execute_subagent_status_tool_call(const ToolCall *tool_call, ToolResult *result) {
    (void)tool_call;
    (void)result;
    // TODO: Implement in Step 8
    return -1;
}

/**
 * Entry point for running ralph as a subagent process.
 * Called from main() when --subagent flag is present.
 *
 * The subagent runs with:
 * - Fresh conversation context (no parent history inheritance)
 * - Output written to stdout (captured by parent via pipe)
 * - Standard ralph capabilities except subagent tools (to prevent nesting)
 *
 * @param task Task description to execute (required)
 * @param context Optional context information to prepend to task
 * @return 0 on success, non-zero on failure
 */
int ralph_run_as_subagent(const char *task, const char *context) {
    if (task == NULL || strlen(task) == 0) {
        fprintf(stderr, "Error: Subagent requires a task\n");
        return -1;
    }

    RalphSession session;

    // Initialize session
    if (ralph_init_session(&session) != 0) {
        fprintf(stderr, "Error: Failed to initialize subagent session\n");
        return -1;
    }

    // Clear conversation history for fresh context (subagents don't inherit parent history)
    // This ensures isolation between parent and child conversations
    cleanup_conversation_history(&session.session_data.conversation);
    init_conversation_history(&session.session_data.conversation);

    // Load configuration
    if (ralph_load_config(&session) != 0) {
        fprintf(stderr, "Error: Failed to load subagent configuration\n");
        ralph_cleanup_session(&session);
        return -1;
    }

    // Build message with optional context
    char *message = NULL;
    if (context != NULL && strlen(context) > 0) {
        // Combine context and task
        const char *format = "Context: %s\n\nTask: %s";
        size_t len = strlen(format) + strlen(context) + strlen(task) + 1;
        message = malloc(len);
        if (message == NULL) {
            fprintf(stderr, "Error: Failed to allocate message buffer\n");
            ralph_cleanup_session(&session);
            return -1;
        }
        snprintf(message, len, format, context, task);
    } else {
        // Just the task
        message = strdup(task);
        if (message == NULL) {
            fprintf(stderr, "Error: Failed to allocate message buffer\n");
            ralph_cleanup_session(&session);
            return -1;
        }
    }

    // Process the message
    // Output automatically goes to stdout (via print_formatted_response_improved)
    // which the parent process captures through the pipe
    int result = ralph_process_message(&session, message);

    // Clean up
    free(message);
    ralph_cleanup_session(&session);

    return result;
}
