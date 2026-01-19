#include "subagent_tool.h"
#include "../utils/config.h"
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

/*
 * The following functions are stubs that will be implemented in later phases:
 * - subagent_spawn()
 * - subagent_get_status()
 * - register_subagent_tool()
 * - register_subagent_status_tool()
 * - execute_subagent_tool_call()
 * - execute_subagent_status_tool_call()
 * - ralph_run_as_subagent()
 */

int subagent_spawn(SubagentManager *manager, const char *task, const char *context, char *subagent_id_out) {
    (void)manager;
    (void)task;
    (void)context;
    (void)subagent_id_out;
    // TODO: Implement in Step 4
    return -1;
}

int subagent_get_status(SubagentManager *manager, const char *subagent_id, int wait,
                        SubagentStatus *status, char **result, char **error) {
    (void)manager;
    (void)subagent_id;
    (void)wait;
    (void)status;
    (void)result;
    (void)error;
    // TODO: Implement in Step 5
    return -1;
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

int ralph_run_as_subagent(const char *task, const char *context) {
    (void)task;
    (void)context;
    // TODO: Implement in Step 7
    return -1;
}
