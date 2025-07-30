#include "shell_tool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

static volatile pid_t executing_pid = 0;

static void timeout_handler(int sig) {
    (void)sig;
    if (executing_pid > 0) {
        kill(executing_pid, SIGKILL);  // Use SIGKILL for immediate termination
    }
}

int register_shell_tool(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }
    
    // Expand registry if needed
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (registry->function_count + 1) * sizeof(ToolFunction));
    if (new_functions == NULL) {
        return -1;
    }
    
    registry->functions = new_functions;
    ToolFunction *shell_func = &registry->functions[registry->function_count];
    
    // Initialize shell tool function
    shell_func->name = strdup("shell_execute");
    shell_func->description = strdup("Execute shell commands on the host system. Returns stdout, stderr, exit code, and execution time.");
    
    if (shell_func->name == NULL || shell_func->description == NULL) {
        free(shell_func->name);
        free(shell_func->description);
        return -1;
    }
    
    // Define parameters
    shell_func->parameter_count = 4;
    shell_func->parameters = malloc(4 * sizeof(ToolParameter));
    if (shell_func->parameters == NULL) {
        free(shell_func->name);
        free(shell_func->description);
        return -1;
    }
    
    // Parameter 1: command (required)
    shell_func->parameters[0].name = strdup("command");
    shell_func->parameters[0].type = strdup("string");
    shell_func->parameters[0].description = strdup("Shell command to execute");
    shell_func->parameters[0].enum_values = NULL;
    shell_func->parameters[0].enum_count = 0;
    shell_func->parameters[0].required = 1;
    
    // Parameter 2: working_directory (optional)
    shell_func->parameters[1].name = strdup("working_directory");
    shell_func->parameters[1].type = strdup("string");
    shell_func->parameters[1].description = strdup("Working directory for command execution (optional)");
    shell_func->parameters[1].enum_values = NULL;
    shell_func->parameters[1].enum_count = 0;
    shell_func->parameters[1].required = 0;
    
    // Parameter 3: timeout_seconds (optional)
    shell_func->parameters[2].name = strdup("timeout_seconds");
    shell_func->parameters[2].type = strdup("number");
    shell_func->parameters[2].description = strdup("Timeout in seconds (0 for no timeout, max 300)");
    shell_func->parameters[2].enum_values = NULL;
    shell_func->parameters[2].enum_count = 0;
    shell_func->parameters[2].required = 0;
    
    // Parameter 4: capture_stderr (optional)
    shell_func->parameters[3].name = strdup("capture_stderr");
    shell_func->parameters[3].type = strdup("boolean");
    shell_func->parameters[3].description = strdup("Whether to capture stderr separately (default: true)");
    shell_func->parameters[3].enum_values = NULL;
    shell_func->parameters[3].enum_count = 0;
    shell_func->parameters[3].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 4; i++) {
        if (shell_func->parameters[i].name == NULL || 
            shell_func->parameters[i].type == NULL ||
            shell_func->parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(shell_func->parameters[j].name);
                free(shell_func->parameters[j].type);
                free(shell_func->parameters[j].description);
            }
            free(shell_func->parameters);
            free(shell_func->name);
            free(shell_func->description);
            return -1;
        }
    }
    
    registry->function_count++;
    return 0;
}

int validate_shell_command(const char *command) {
    if (command == NULL || strlen(command) == 0) {
        return 0;
    }
    
    if (strlen(command) > SHELL_MAX_COMMAND_LENGTH) {
        return 0;
    }
    
    // Basic safety checks - reject commands with potentially dangerous patterns
    const char *dangerous_patterns[] = {
        "rm -rf /",
        "rm -rf /*",
        "mkfs",
        "dd if=",
        ":(){ :|:& };:",  // Fork bomb
        "chmod -R 777 /",
        NULL
    };
    
    for (int i = 0; dangerous_patterns[i] != NULL; i++) {
        if (strstr(command, dangerous_patterns[i]) != NULL) {
            return 0;
        }
    }
    
    return 1;
}

static char* extract_json_string_value(const char *json, const char *key) {
    char search_pattern[256] = {0};
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    
    const char *key_pos = strstr(json, search_pattern);
    if (key_pos == NULL) {
        return NULL;
    }
    
    const char *colon_pos = strchr(key_pos, ':');
    if (colon_pos == NULL) {
        return NULL;
    }
    
    colon_pos++;
    while (*colon_pos == ' ' || *colon_pos == '\t') {
        colon_pos++;
    }
    
    if (*colon_pos != '"') {
        return NULL;
    }
    
    const char *start = colon_pos + 1;
    const char *end = start;
    
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && *(end + 1) != '\0') {
            end += 2;
        } else {
            end++;
        }
    }
    
    if (*end != '"') {
        return NULL;
    }
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }
    
    memcpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

static int extract_json_number_value(const char *json, const char *key) {
    char search_pattern[256];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    
    const char *key_pos = strstr(json, search_pattern);
    if (key_pos == NULL) {
        return 0;
    }
    
    const char *colon_pos = strchr(key_pos, ':');
    if (colon_pos == NULL) {
        return 0;
    }
    
    colon_pos++;
    while (*colon_pos == ' ' || *colon_pos == '\t') {
        colon_pos++;
    }
    
    return atoi(colon_pos);
}

static int extract_json_boolean_value(const char *json, const char *key) {
    char search_pattern[256];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    
    const char *key_pos = strstr(json, search_pattern);
    if (key_pos == NULL) {
        return 1; // Default to true
    }
    
    const char *colon_pos = strchr(key_pos, ':');
    if (colon_pos == NULL) {
        return 1;
    }
    
    colon_pos++;
    while (*colon_pos == ' ' || *colon_pos == '\t') {
        colon_pos++;
    }
    
    return (strncmp(colon_pos, "true", 4) == 0) ? 1 : 0;
}

int parse_shell_arguments(const char *json_args, ShellCommandParams *params) {
    if (json_args == NULL || params == NULL) {
        return -1;
    }
    
    memset(params, 0, sizeof(ShellCommandParams));
    
    params->command = extract_json_string_value(json_args, "command");
    if (params->command == NULL) {
        return -1;
    }
    
    params->working_directory = extract_json_string_value(json_args, "working_directory");
    params->timeout_seconds = extract_json_number_value(json_args, "timeout_seconds");
    params->capture_stderr = extract_json_boolean_value(json_args, "capture_stderr");
    
    // Validate timeout
    if (params->timeout_seconds > SHELL_MAX_TIMEOUT_SECONDS) {
        params->timeout_seconds = SHELL_MAX_TIMEOUT_SECONDS;
    }
    
    return 0;
}

int execute_shell_command(const ShellCommandParams *params, ShellExecutionResult *result) {
    if (params == NULL || result == NULL || params->command == NULL) {
        return -1;
    }
    
    memset(result, 0, sizeof(ShellExecutionResult));
    
    if (!validate_shell_command(params->command)) {
        result->stdout_output = strdup("Error: Command failed security validation");
        result->stderr_output = strdup("");
        result->exit_code = -1;
        result->execution_time = 0.0;
        result->timed_out = 0;
        return 0; // Return success but with error message
    }
    
    int stdout_pipe[2], stderr_pipe[2];
    pid_t pid;
    struct timeval start_time, end_time;
    
    if (pipe(stdout_pipe) == -1) {
        return -1;
    }
    
    if (params->capture_stderr && pipe(stderr_pipe) == -1) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return -1;
    }
    
    gettimeofday(&start_time, NULL);
    
    pid = fork();
    if (pid == -1) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        if (params->capture_stderr) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);
        if (params->capture_stderr) {
            close(stderr_pipe[0]);
        }
        
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[1]);
        
        if (params->capture_stderr) {
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stderr_pipe[1]);
        } else {
            dup2(stdout_pipe[1], STDERR_FILENO);
        }
        
        // Change working directory if specified
        if (params->working_directory != NULL) {
            if (chdir(params->working_directory) != 0) {
                fprintf(stderr, "Failed to change directory to %s: %s\n", 
                       params->working_directory, strerror(errno));
                exit(1);
            }
        }
        
        // Execute command
        execl("/bin/sh", "sh", "-c", params->command, (char *)NULL);
        fprintf(stderr, "Failed to execute command: %s\n", strerror(errno));
        exit(1);
    }
    
    // Parent process
    executing_pid = pid;
    close(stdout_pipe[1]);
    if (params->capture_stderr) {
        close(stderr_pipe[1]);
    }
    
    // Setup timeout handling
    struct sigaction sa;
    sa.sa_handler = timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    
    // Set up timeout for the entire operation
    if (params->timeout_seconds > 0) {
        alarm(params->timeout_seconds);
    }
    
    // Wait for child process with timeout using time-based polling
    int status;
    int wait_result;
    result->timed_out = 0;
    
    if (params->timeout_seconds > 0) {
        // Time-based timeout approach
        struct timeval timeout_start, current_time;
        gettimeofday(&timeout_start, NULL);
        
        while (1) {
            wait_result = waitpid(pid, &status, WNOHANG);
            if (wait_result == pid) {
                // Process completed normally
                result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                alarm(0);  // Cancel alarm
                break;
            } else if (wait_result == -1) {
                // Error occurred
                result->exit_code = -1;
                alarm(0);
                break;
            }
            
            // Check if timeout exceeded
            gettimeofday(&current_time, NULL);
            double elapsed = (current_time.tv_sec - timeout_start.tv_sec) + 
                           (current_time.tv_usec - timeout_start.tv_usec) / 1000000.0;
            
            if (elapsed >= params->timeout_seconds) {
                result->timed_out = 1;
                result->exit_code = -1;
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);  // Clean up zombie
                alarm(0);
                break;
            }
            
            // Process still running, sleep briefly and continue
            usleep(50000);  // Sleep 50ms
        }
    } else {
        // No timeout, wait normally
        wait_result = waitpid(pid, &status, 0);
        if (wait_result == pid) {
            result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        } else {
            result->exit_code = -1;
        }
    }
    
    // Read output after process completion (non-blocking since process is done)
    char stdout_buffer[SHELL_MAX_OUTPUT_LENGTH];
    char stderr_buffer[SHELL_MAX_OUTPUT_LENGTH];
    ssize_t stdout_bytes = 0, stderr_bytes = 0;
    
    stdout_bytes = read(stdout_pipe[0], stdout_buffer, sizeof(stdout_buffer) - 1);
    if (stdout_bytes < 0) stdout_bytes = 0;
    stdout_buffer[stdout_bytes] = '\0';
    
    if (params->capture_stderr) {
        stderr_bytes = read(stderr_pipe[0], stderr_buffer, sizeof(stderr_buffer) - 1);
        if (stderr_bytes < 0) stderr_bytes = 0;
        stderr_buffer[stderr_bytes] = '\0';
    } else {
        stderr_buffer[0] = '\0';
    }
    
    close(stdout_pipe[0]);
    if (params->capture_stderr) {
        close(stderr_pipe[0]);
    }
    
    executing_pid = 0;
    
    gettimeofday(&end_time, NULL);
    
    // Calculate execution time, but if timed out, limit it to the timeout duration
    double actual_time = (end_time.tv_sec - start_time.tv_sec) + 
                        (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    if (result->timed_out && params->timeout_seconds > 0) {
        // For timed out processes, execution time should reflect the timeout duration
        result->execution_time = params->timeout_seconds;
    } else {
        result->execution_time = actual_time;
    }
    
    // Allocate and copy output
    result->stdout_output = malloc(stdout_bytes + 1);
    result->stderr_output = malloc(stderr_bytes + 1);
    
    if (result->stdout_output == NULL || result->stderr_output == NULL) {
        free(result->stdout_output);
        free(result->stderr_output);
        return -1;
    }
    
    strcpy(result->stdout_output, stdout_buffer);
    strcpy(result->stderr_output, stderr_buffer);
    
    return 0;
}

char* format_shell_result_json(const ShellExecutionResult *exec_result) {
    if (exec_result == NULL) {
        return NULL;
    }
    
    size_t estimated_size = 1000 + 
                           (exec_result->stdout_output ? strlen(exec_result->stdout_output) * 2 : 0) +
                           (exec_result->stderr_output ? strlen(exec_result->stderr_output) * 2 : 0);
    
    char *json = malloc(estimated_size);
    if (json == NULL) {
        return NULL;
    }
    
    snprintf(json, estimated_size,
        "{"
        "\"stdout\": \"%s\", "
        "\"stderr\": \"%s\", "
        "\"exit_code\": %d, "
        "\"execution_time\": %.3f, "
        "\"timed_out\": %s"
        "}",
        exec_result->stdout_output ? exec_result->stdout_output : "",
        exec_result->stderr_output ? exec_result->stderr_output : "",
        exec_result->exit_code,
        exec_result->execution_time,
        exec_result->timed_out ? "true" : "false"
    );
    
    return json;
}

int execute_shell_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }
    
    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }
    
    ShellCommandParams params;
    if (parse_shell_arguments(tool_call->arguments, &params) != 0) {
        result->result = strdup("Error: Failed to parse shell command arguments");
        result->success = 0;
        return 0;
    }
    
    ShellExecutionResult exec_result;
    if (execute_shell_command(&params, &exec_result) != 0) {
        cleanup_shell_params(&params);
        result->result = strdup("Error: Failed to execute shell command");
        result->success = 0;
        return 0;
    }
    
    result->result = format_shell_result_json(&exec_result);
    result->success = (exec_result.exit_code == 0 && !exec_result.timed_out) ? 1 : 0;
    
    cleanup_shell_params(&params);
    cleanup_shell_result(&exec_result);
    
    if (result->result == NULL) {
        return -1;
    }
    
    return 0;
}

void cleanup_shell_params(ShellCommandParams *params) {
    if (params == NULL) {
        return;
    }
    
    free(params->command);
    free(params->working_directory);
    
    if (params->environment != NULL) {
        for (int i = 0; params->environment[i] != NULL; i++) {
            free(params->environment[i]);
        }
        free(params->environment);
    }
    
    memset(params, 0, sizeof(ShellCommandParams));
}

void cleanup_shell_result(ShellExecutionResult *result) {
    if (result == NULL) {
        return;
    }
    
    free(result->stdout_output);
    free(result->stderr_output);
    
    memset(result, 0, sizeof(ShellExecutionResult));
}