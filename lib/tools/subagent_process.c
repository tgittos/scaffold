/*
 * subagent_process.c - Subagent Process I/O and Lifecycle
 *
 * Functions for reading subagent output, handling process exit,
 * and managing subagent resources. Extracted from subagent_tool.c.
 */

#include "subagent_process.h"
#include "messaging_tool.h"
#include "../ipc/message_store.h"
#include "../services/services.h"
#include "../util/debug_output.h"
#include "../util/executable_path.h"

#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

void generate_subagent_id(char *id_out) {
    static const char hex[] = "0123456789abcdef";
    unsigned char random_bytes[SUBAGENT_ID_LENGTH / 2];

    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t read_count = fread(random_bytes, 1, sizeof(random_bytes), f);
        fclose(f);
        if (read_count != sizeof(random_bytes)) {
            srand((unsigned int)(time(NULL) ^ getpid()));
            for (size_t i = read_count; i < sizeof(random_bytes); i++) {
                random_bytes[i] = (unsigned char)(rand() & 0xFF);
            }
        }
    } else {
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

/*
 * Clean up resources for a single subagent.
 * Closes pipes and frees allocated strings.
 *
 * Note: We use > 0 checks for stdout_pipe (original behavior) and > 2 checks
 * for approval_channel FDs to avoid closing stdin/stdout/stderr (0,1,2) when
 * the struct was zero-initialized with memset.
 */
void cleanup_subagent(Subagent *sub, Services *services) {
    if (sub == NULL) {
        return;
    }

    if (sub->stdout_pipe[0] > 0) {
        close(sub->stdout_pipe[0]);
        sub->stdout_pipe[0] = -1;
    }
    if (sub->stdout_pipe[1] > 0) {
        close(sub->stdout_pipe[1]);
        sub->stdout_pipe[1] = -1;
    }

    if (sub->approval_channel.request_fd > 2) {
        close(sub->approval_channel.request_fd);
        sub->approval_channel.request_fd = -1;
    }
    if (sub->approval_channel.response_fd > 2) {
        close(sub->approval_channel.response_fd);
        sub->approval_channel.response_fd = -1;
    }
    sub->approval_channel.subagent_pid = 0;

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

    if (sub->id[0] != '\0') {
        message_store_t* msg_store = services_get_message_store(services);
        if (msg_store != NULL) {
            message_cleanup_agent(msg_store, sub->id);
        }
    }
}

int read_subagent_output_nonblocking(Subagent *sub) {
    if (sub == NULL || sub->stdout_pipe[0] <= 0) {
        return -1;
    }

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
                break;
            }
            if (bytes_read == 0) {
                break;
            }
            return -1;
        }

        buffer[bytes_read] = '\0';
        total_read += (int)bytes_read;

        size_t new_len = sub->output_len + (size_t)bytes_read;
        if (new_len > SUBAGENT_MAX_OUTPUT_LENGTH) {
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

    fcntl(sub->stdout_pipe[0], F_SETFL, flags);

    return total_read;
}

int read_subagent_output(Subagent *sub) {
    if (sub == NULL || sub->stdout_pipe[0] <= 0) {
        return -1;
    }

    char buffer[4096];

    while (1) {
        ssize_t bytes_read = read(sub->stdout_pipe[0], buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                break;
            }
            return -1;
        }

        buffer[bytes_read] = '\0';

        size_t new_len = sub->output_len + (size_t)bytes_read;
        if (new_len > SUBAGENT_MAX_OUTPUT_LENGTH) {
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

    close(sub->stdout_pipe[0]);
    sub->stdout_pipe[0] = -1;

    return 0;
}

void subagent_handle_process_exit(Subagent *sub, int proc_status) {
    if (sub == NULL) {
        return;
    }

    read_subagent_output(sub);

    if (WIFEXITED(proc_status) && WEXITSTATUS(proc_status) == 0) {
        sub->status = SUBAGENT_STATUS_COMPLETED;
        sub->result = sub->output;
        sub->output = NULL;
        sub->output_len = 0;
    } else {
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

        free(sub->output);
        sub->output = NULL;
        sub->output_len = 0;
    }
}

void subagent_notify_parent(const Subagent* sub, Services *services) {
    if (sub == NULL) {
        return;
    }

    char* parent_id = messaging_tool_get_agent_id();
    if (parent_id == NULL || parent_id[0] == '\0') {
        debug_printf("subagent_notify_parent: no parent agent ID set, skipping notification\n");
        free(parent_id);
        return;
    }

    message_store_t* store = services_get_message_store(services);
    if (store == NULL) {
        debug_printf("subagent_notify_parent: message store unavailable\n");
        free(parent_id);
        return;
    }

    cJSON* msg = cJSON_CreateObject();
    if (msg == NULL) {
        debug_printf("subagent_notify_parent: failed to create JSON object\n");
        free(parent_id);
        return;
    }

    cJSON_AddStringToObject(msg, "type", "subagent_completion");
    cJSON_AddStringToObject(msg, "subagent_id", sub->id);
    cJSON_AddStringToObject(msg, "status",
        sub->status == SUBAGENT_STATUS_COMPLETED ? "completed" :
        sub->status == SUBAGENT_STATUS_FAILED ? "failed" :
        sub->status == SUBAGENT_STATUS_TIMEOUT ? "timeout" : "unknown");

    if (sub->result != NULL) {
        cJSON_AddStringToObject(msg, "result", sub->result);
    }
    if (sub->error != NULL) {
        cJSON_AddStringToObject(msg, "error", sub->error);
    }
    if (sub->task != NULL) {
        cJSON_AddStringToObject(msg, "task", sub->task);
    }

    if (sub->start_time > 0) {
        int elapsed = (int)(time(NULL) - sub->start_time);
        cJSON_AddNumberToObject(msg, "elapsed_seconds", elapsed);
    }

    char* json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);

    if (json_str == NULL) {
        debug_printf("subagent_notify_parent: failed to serialize JSON\n");
        free(parent_id);
        return;
    }

    char msg_id[40];
    int result = message_send_direct(store, sub->id, parent_id, json_str, 0, msg_id);
    if (result != 0) {
        debug_printf("subagent_notify_parent: failed to send message to parent\n");
    } else {
        debug_printf("subagent_notify_parent: sent completion message %s to parent %s\n",
                     msg_id, parent_id);
    }
    free(json_str);
    free(parent_id);
}

char* subagent_get_executable_path(void) {
    return get_executable_path();
}
