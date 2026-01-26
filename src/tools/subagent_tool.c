#include "subagent_tool.h"
#include "../utils/config.h"
#include "../core/ralph.h"
#include "../core/subagent_approval.h"
#include "../session/conversation_tracker.h"
#include <cJSON.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

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
 */
static int init_subagent_approval_channel(void) {
    const char *request_fd_str = getenv(RALPH_APPROVAL_REQUEST_FD);
    const char *response_fd_str = getenv(RALPH_APPROVAL_RESPONSE_FD);

    if (request_fd_str == NULL || response_fd_str == NULL) {
        /* Not running as subagent or parent didn't set up approval channel */
        return -1;
    }

    int request_fd = atoi(request_fd_str);
    int response_fd = atoi(response_fd_str);

    if (request_fd < 0 || response_fd < 0) {
        return -1;
    }

    g_subagent_approval_channel = malloc(sizeof(ApprovalChannel));
    if (g_subagent_approval_channel == NULL) {
        return -1;
    }

    g_subagent_approval_channel->request_fd = request_fd;
    g_subagent_approval_channel->response_fd = response_fd;
    g_subagent_approval_channel->subagent_pid = getpid();

    return 0;
}

/**
 * Clean up the subagent approval channel.
 * Called during subagent process cleanup.
 */
static void cleanup_subagent_approval_channel(void) {
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

/**
 * Get the approval channel for this subagent process.
 * Returns NULL if not running as a subagent.
 */
ApprovalChannel* subagent_get_approval_channel(void) {
    return g_subagent_approval_channel;
}

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
 *
 * Note: We use > 0 checks for stdout_pipe (original behavior) and > 2 checks
 * for approval_channel FDs to avoid closing stdin/stdout/stderr (0,1,2) when
 * the struct was zero-initialized with memset.
 */
void cleanup_subagent(Subagent *sub) {
    if (sub == NULL) {
        return;
    }

    // Close stdout pipe ends if still open (using > 0, original behavior)
    if (sub->stdout_pipe[0] > 0) {
        close(sub->stdout_pipe[0]);
        sub->stdout_pipe[0] = -1;
    }
    if (sub->stdout_pipe[1] > 0) {
        close(sub->stdout_pipe[1]);
        sub->stdout_pipe[1] = -1;
    }

    // Close approval channel pipe ends if still open
    // Use > 2 to skip stdin/stdout/stderr (0,1,2) which could be set by memset(0)
    if (sub->approval_channel.request_fd > 2) {
        close(sub->approval_channel.request_fd);
        sub->approval_channel.request_fd = -1;
    }
    if (sub->approval_channel.response_fd > 2) {
        close(sub->approval_channel.response_fd);
        sub->approval_channel.response_fd = -1;
    }
    sub->approval_channel.subagent_pid = 0;

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
    for (size_t i = 0; i < manager->subagents.count; i++) {
        Subagent *sub = &manager->subagents.data[i];

        // Kill running processes
        if (sub->status == SUBAGENT_STATUS_RUNNING && sub->pid > 0) {
            // Send SIGTERM first for graceful shutdown
            kill(sub->pid, SIGTERM);

            // Give it a brief moment to exit
            int status;
            pid_t result = waitpid(sub->pid, &status, WNOHANG);
            if (result == 0) {
                // Still running, use SIGKILL
                usleep(SUBAGENT_GRACE_PERIOD_USEC);
                kill(sub->pid, SIGKILL);
                waitpid(sub->pid, &status, 0);
            }
        }

        // Clean up subagent resources
        cleanup_subagent(sub);
    }

    // Free the subagents array
    SubagentArray_destroy(&manager->subagents);
}

/**
 * Find a subagent by its ID.
 */
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
 * Handle process exit status and update subagent state accordingly.
 * Reads any remaining output, then sets status to COMPLETED or FAILED
 * based on the process exit code or signal.
 *
 * @param sub Pointer to the Subagent structure
 * @param proc_status Process status from waitpid()
 */
static void handle_process_exit(Subagent *sub, int proc_status) {
    if (sub == NULL) {
        return;
    }

    // Read any remaining output
    read_subagent_output(sub);

    if (WIFEXITED(proc_status) && WEXITSTATUS(proc_status) == 0) {
        // Successful exit - move output to result
        sub->status = SUBAGENT_STATUS_COMPLETED;
        sub->result = sub->output;
        sub->output = NULL;
        sub->output_len = 0;
    } else {
        // Failed exit - create error message
        sub->status = SUBAGENT_STATUS_FAILED;

        char error_msg[256];
        if (WIFEXITED(proc_status)) {
            snprintf(error_msg, sizeof(error_msg),
                     "Subagent exited with code %d", WEXITSTATUS(proc_status));
        } else if (WIFSIGNALED(proc_status)) {
            snprintf(error_msg, sizeof(error_msg),
                     "Subagent killed by signal %d", WTERMSIG(proc_status));
        } else {
            snprintf(error_msg, sizeof(error_msg), "Subagent process failed");
        }

        // Append captured output if available for debugging
        if (sub->output != NULL && sub->output_len > 0) {
            size_t error_len = strlen(error_msg) + sub->output_len + 32;
            char *full_error = malloc(error_len);
            if (full_error != NULL) {
                snprintf(full_error, error_len, "%s. Output: %s", error_msg, sub->output);
                sub->error = full_error;
            } else {
                sub->error = strdup(error_msg);
            }
        } else {
            sub->error = strdup(error_msg);
        }

        // Free output after incorporating it into error message (prevents memory leak)
        free(sub->output);
        sub->output = NULL;
        sub->output_len = 0;
    }
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
            // Process has exited - delegate to helper
            handle_process_exit(sub, proc_status);
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
 *
 * Note: APE binaries run via an extracted loader (e.g., /root/.ape-1.10),
 * so /proc/self/exe returns the loader path, not the actual binary.
 * We detect this and fall back to finding ralph in the current directory.
 */
static char* get_executable_path(void) {
    char *path = malloc(SUBAGENT_PATH_BUFFER_SIZE);
    if (path == NULL) {
        return NULL;
    }

    // Try /proc/self/exe first (Linux)
    ssize_t len = readlink("/proc/self/exe", path, SUBAGENT_PATH_BUFFER_SIZE - 1);
    if (len > 0) {
        path[len] = '\0';
        // Check if this is the APE loader (contains ".ape-" in path)
        // If so, skip and use fallback methods
        if (strstr(path, ".ape-") == NULL) {
            return path;
        }
    }

    // Fallback: try current directory
    if (getcwd(path, SUBAGENT_PATH_BUFFER_SIZE) != NULL) {
        size_t cwd_len = strlen(path);
        if (cwd_len + 7 < SUBAGENT_PATH_BUFFER_SIZE) {  // "/ralph" + null
            strcat(path, "/ralph");
            if (access(path, X_OK) == 0) {
                return path;
            }
        }
    }

    // Last fallback: assume ./ralph
    free(path);
    return strdup("./ralph");
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

    // Prevent nesting: don't allow subagents to spawn subagents
    if (manager->is_subagent_process) {
        return -1;
    }

    // Check max limit (compare as size_t to avoid overflow on cast)
    if (manager->max_subagents <= 0 || manager->subagents.count >= (size_t)manager->max_subagents) {
        return -1;
    }

    // Generate unique ID
    char id[SUBAGENT_ID_LENGTH + 1];
    generate_subagent_id(id);

    // Create pipe for capturing child output
    int stdout_pipefd[2];
    if (pipe(stdout_pipefd) == -1) {
        return -1;
    }

    // Create approval channel pipes for IPC-based approval proxying
    int request_pipe[2], response_pipe[2];
    if (create_approval_channel_pipes(request_pipe, response_pipe) < 0) {
        close(stdout_pipefd[0]);
        close(stdout_pipefd[1]);
        return -1;
    }

    // Get path to ralph executable
    char *ralph_path = get_executable_path();
    if (ralph_path == NULL) {
        close(stdout_pipefd[0]);
        close(stdout_pipefd[1]);
        cleanup_approval_channel_pipes(request_pipe, response_pipe);
        return -1;
    }

    // Fork the process
    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        free(ralph_path);
        close(stdout_pipefd[0]);
        close(stdout_pipefd[1]);
        cleanup_approval_channel_pipes(request_pipe, response_pipe);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(stdout_pipefd[0]);  // Close read end of stdout pipe

        // Redirect stdout to pipe
        if (dup2(stdout_pipefd[1], STDOUT_FILENO) == -1) {
            _exit(127);
        }
        close(stdout_pipefd[1]);

        // Also redirect stderr to stdout so we capture errors
        if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
            _exit(127);
        }

        // Set up approval channel for child (closes parent ends)
        // Child writes requests (request_pipe[1]) and reads responses (response_pipe[0])
        close(request_pipe[0]);   // Close read end of request pipe
        close(response_pipe[1]);  // Close write end of response pipe

        // Pass approval channel FDs to child via environment variables
        char request_fd_str[32], response_fd_str[32];
        snprintf(request_fd_str, sizeof(request_fd_str), "%d", request_pipe[1]);
        snprintf(response_fd_str, sizeof(response_fd_str), "%d", response_pipe[0]);
        setenv(RALPH_APPROVAL_REQUEST_FD, request_fd_str, 1);
        setenv(RALPH_APPROVAL_RESPONSE_FD, response_fd_str, 1);

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
    close(stdout_pipefd[1]);  // Close write end of stdout pipe

    // Set up approval channel for parent (closes child ends)
    // Parent reads requests (request_pipe[0]) and writes responses (response_pipe[1])
    close(request_pipe[1]);   // Close write end of request pipe
    close(response_pipe[0]);  // Close read end of response pipe

    // Initialize the new subagent entry
    Subagent new_sub;
    memset(&new_sub, 0, sizeof(Subagent));

    memcpy(new_sub.id, id, SUBAGENT_ID_LENGTH);
    new_sub.id[SUBAGENT_ID_LENGTH] = '\0';
    new_sub.pid = pid;
    new_sub.status = SUBAGENT_STATUS_RUNNING;
    new_sub.stdout_pipe[0] = stdout_pipefd[0];
    new_sub.stdout_pipe[1] = -1;  // Write end closed in parent
    new_sub.approval_channel.request_fd = request_pipe[0];   // Parent reads requests
    new_sub.approval_channel.response_fd = response_pipe[1]; // Parent writes responses
    new_sub.approval_channel.subagent_pid = pid;
    new_sub.task = strdup(task);
    new_sub.context = (context != NULL && strlen(context) > 0) ? strdup(context) : NULL;
    new_sub.output = NULL;
    new_sub.output_len = 0;
    new_sub.result = NULL;
    new_sub.error = NULL;
    new_sub.start_time = time(NULL);

    // Check if strdup failed
    if (new_sub.task == NULL || (context != NULL && strlen(context) > 0 && new_sub.context == NULL)) {
        // Cleanup on allocation failure
        if (new_sub.task) free(new_sub.task);
        if (new_sub.context) free(new_sub.context);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(stdout_pipefd[0]);
        close(request_pipe[0]);
        close(response_pipe[1]);
        return -1;
    }

    // Add to array
    if (SubagentArray_push(&manager->subagents, new_sub) != 0) {
        // Array push failed - kill the child and clean up
        if (new_sub.task) free(new_sub.task);
        if (new_sub.context) free(new_sub.context);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(stdout_pipefd[0]);
        close(request_pipe[0]);
        close(response_pipe[1]);
        return -1;
    }

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
            // Process has exited - delegate to helper
            handle_process_exit(sub, proc_status);
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
            // Process has exited - delegate to helper
            handle_process_exit(sub, proc_status);
            break;
        } else if (waitpid_result == -1 && errno != ECHILD) {
            // Error in waitpid
            sub->status = SUBAGENT_STATUS_FAILED;
            sub->error = strdup("Failed to check subagent status");
            break;
        }

        // Still running, sleep briefly before checking again
        usleep(SUBAGENT_POLL_INTERVAL_USEC);
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
 * Escape a string for JSON output.
 * Returns a newly allocated string that must be freed by the caller.
 */
static char* json_escape_string(const char *str) {
    if (str == NULL) {
        return strdup("");
    }

    // Calculate required size
    size_t len = 0;
    for (const char *p = str; *p != '\0'; p++) {
        switch (*p) {
            case '"':
            case '\\':
            case '\n':
            case '\r':
            case '\t':
                len += 2;
                break;
            default:
                if ((unsigned char)*p < 0x20) {
                    len += 6;  // \uXXXX
                } else {
                    len += 1;
                }
                break;
        }
    }

    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }

    char *out = result;
    for (const char *p = str; *p != '\0'; p++) {
        switch (*p) {
            case '"':
                *out++ = '\\';
                *out++ = '"';
                break;
            case '\\':
                *out++ = '\\';
                *out++ = '\\';
                break;
            case '\n':
                *out++ = '\\';
                *out++ = 'n';
                break;
            case '\r':
                *out++ = '\\';
                *out++ = 'r';
                break;
            case '\t':
                *out++ = '\\';
                *out++ = 't';
                break;
            default:
                if ((unsigned char)*p < 0x20) {
                    snprintf(out, 7, "\\u%04x", (unsigned char)*p);
                    out += 6;
                } else {
                    *out++ = *p;
                }
                break;
        }
    }
    *out = '\0';

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

    // Check for potential overwrite of existing manager (prevents subtle bugs)
    if (g_subagent_manager != NULL && g_subagent_manager != manager) {
        fprintf(stderr, "Warning: Overwriting existing subagent manager pointer. "
                        "Only one SubagentManager should be active per process.\n");
    }

    // Store manager pointer for execute function
    g_subagent_manager = manager;

    // Define parameters
    ToolParameter parameters[2];
    memset(parameters, 0, sizeof(parameters));

    // Parameter 1: task (required)
    parameters[0].name = strdup("task");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("Task description for the subagent to execute");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;

    // Parameter 2: context (optional)
    parameters[1].name = strdup("context");
    parameters[1].type = strdup("string");
    parameters[1].description = strdup("Optional context information to provide to the subagent");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;

    // Check for allocation failures
    for (int i = 0; i < 2; i++) {
        if (parameters[i].name == NULL ||
            parameters[i].type == NULL ||
            parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(parameters[j].name);
                free(parameters[j].type);
                free(parameters[j].description);
            }
            return -1;
        }
    }

    // Register the tool
    int result = register_tool(registry, "subagent",
                              "Spawn a background subagent process to execute a delegated task. "
                              "The subagent runs with fresh context and cannot spawn additional subagents. "
                              "Returns a subagent_id that can be used with subagent_status to check progress.",
                              parameters, 2, execute_subagent_tool_call);

    // Clean up temporary parameter storage
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

    // Check for potential overwrite of existing manager (prevents subtle bugs)
    if (g_subagent_manager != NULL && g_subagent_manager != manager) {
        fprintf(stderr, "Warning: Overwriting existing subagent manager pointer. "
                        "Only one SubagentManager should be active per process.\n");
    }

    // Store manager pointer for execute function (may already be set)
    g_subagent_manager = manager;

    // Define parameters
    ToolParameter parameters[2];
    memset(parameters, 0, sizeof(parameters));

    // Parameter 1: subagent_id (required)
    parameters[0].name = strdup("subagent_id");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("ID of the subagent to query status for");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;

    // Parameter 2: wait (optional)
    parameters[1].name = strdup("wait");
    parameters[1].type = strdup("boolean");
    parameters[1].description = strdup("If true, block until the subagent completes (default: false)");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;

    // Check for allocation failures
    for (int i = 0; i < 2; i++) {
        if (parameters[i].name == NULL ||
            parameters[i].type == NULL ||
            parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(parameters[j].name);
                free(parameters[j].type);
                free(parameters[j].description);
            }
            return -1;
        }
    }

    // Register the tool
    int result = register_tool(registry, "subagent_status",
                              "Query the status of a running or completed subagent. "
                              "Returns status (running/completed/failed/timeout), progress, result, and any errors.",
                              parameters, 2, execute_subagent_status_tool_call);

    // Clean up temporary parameter storage
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

    // Initialize result
    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }

    // Check if manager is available
    if (g_subagent_manager == NULL) {
        result->result = strdup("{\"error\": \"Subagent manager not initialized\"}");
        result->success = 0;
        return 0;
    }

    // Check if we're already running as a subagent (prevent nesting)
    if (g_subagent_manager->is_subagent_process) {
        result->result = strdup("{\"error\": \"Subagents cannot spawn additional subagents\"}");
        result->success = 0;
        return 0;
    }

    // Parse arguments
    char *task = extract_json_string_value(tool_call->arguments, "task");
    if (task == NULL || strlen(task) == 0) {
        free(task);
        result->result = strdup("{\"error\": \"Task parameter is required\"}");
        result->success = 0;
        return 0;
    }

    char *context = extract_json_string_value(tool_call->arguments, "context");

    // Spawn the subagent
    char subagent_id[SUBAGENT_ID_LENGTH + 1];
    int spawn_result = subagent_spawn(g_subagent_manager, task, context, subagent_id);

    free(task);
    free(context);

    if (spawn_result != 0) {
        // Check if we hit the max limit
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

    // Build success response
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

    // Initialize result
    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }

    // Check if manager is available
    if (g_subagent_manager == NULL) {
        result->result = strdup("{\"error\": \"Subagent manager not initialized\"}");
        result->success = 0;
        return 0;
    }

    // Parse arguments
    char *subagent_id = extract_json_string_value(tool_call->arguments, "subagent_id");
    if (subagent_id == NULL || strlen(subagent_id) == 0) {
        free(subagent_id);
        result->result = strdup("{\"error\": \"subagent_id parameter is required\"}");
        result->success = 0;
        return 0;
    }

    int wait = extract_json_boolean_value(tool_call->arguments, "wait", 0);

    // Get status
    SubagentStatus status;
    char *subagent_result = NULL;
    char *error = NULL;

    int get_result = subagent_get_status(g_subagent_manager, subagent_id, wait,
                                         &status, &subagent_result, &error);

    free(subagent_id);

    if (get_result != 0) {
        // Subagent not found
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

    // Build response based on status
    const char *status_str = subagent_status_to_string(status);

    // Calculate response size
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

    // Format response based on status
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

    // Cleanup
    free(subagent_result);
    free(error);
    free(escaped_result);
    free(escaped_error);

    return 0;
}

/**
 * Entry point for running ralph as a subagent process.
 * Called from main() when --subagent flag is present.
 *
 * The subagent runs with:
 * - Fresh conversation context (no parent history inheritance)
 * - Output written to stdout (captured by parent via pipe)
 * - Standard ralph capabilities except subagent tools (to prevent nesting)
 * - Approval channel for IPC-based approval proxying to parent
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

    // Initialize approval channel from environment variables
    // This enables the subagent to request approvals from the parent process
    if (init_subagent_approval_channel() == 0) {
        // Approval channel successfully initialized - subagent will use IPC for approvals
    }
    // Note: If init fails, subagent will fall back to direct TTY prompting (if available)
    // or denial (if no TTY). This is acceptable for backwards compatibility.

    RalphSession session;

    // Initialize session
    if (ralph_init_session(&session) != 0) {
        fprintf(stderr, "Error: Failed to initialize subagent session\n");
        cleanup_subagent_approval_channel();
        return -1;
    }

    // Mark this process as a subagent to prevent nesting (subagent tools will return error if called)
    session.subagent_manager.is_subagent_process = 1;

    // Clear conversation history for fresh context (subagents don't inherit parent history)
    // This ensures isolation between parent and child conversations
    cleanup_conversation_history(&session.session_data.conversation);
    init_conversation_history(&session.session_data.conversation);

    // Load configuration
    if (ralph_load_config(&session) != 0) {
        fprintf(stderr, "Error: Failed to load subagent configuration\n");
        ralph_cleanup_session(&session);
        cleanup_subagent_approval_channel();
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
            cleanup_subagent_approval_channel();
            return -1;
        }
        snprintf(message, len, format, context, task);
    } else {
        // Just the task
        message = strdup(task);
        if (message == NULL) {
            fprintf(stderr, "Error: Failed to allocate message buffer\n");
            ralph_cleanup_session(&session);
            cleanup_subagent_approval_channel();
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
    cleanup_subagent_approval_channel();

    return result;
}

/* =============================================================================
 * Parent-Side Approval Request Handling
 * ========================================================================== */

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

    // Count running subagents with valid approval channels
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

    // Build poll array
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
            sub->approval_channel.request_fd >= 0) {
            pfds[poll_idx].fd = sub->approval_channel.request_fd;
            pfds[poll_idx].events = POLLIN;
            pfds[poll_idx].revents = 0;
            indices[poll_idx] = (int)i;
            poll_idx++;
        }
    }

    // Poll for data
    int ready = poll(pfds, poll_idx, timeout_ms);
    if (ready <= 0) {
        free(pfds);
        free(indices);
        return -1;
    }

    // Find the first subagent with pending data
    int result = -1;
    for (int i = 0; i < poll_idx; i++) {
        if (pfds[i].revents & POLLIN) {
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

    // Use the subagent approval module to handle the request
    handle_subagent_approval_request(gate_config, &sub->approval_channel);

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

    // Non-blocking poll for pending requests
    int idx = subagent_poll_approval_requests(manager, 0);
    if (idx < 0) {
        return 0; // No pending requests
    }

    // Handle the request
    if (subagent_handle_approval_request(manager, idx, gate_config) == 0) {
        return 1; // Request handled
    }

    return -1; // Error handling request
}
