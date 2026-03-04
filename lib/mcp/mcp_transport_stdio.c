/*
 * mcp_transport_stdio.c - STDIO Transport Implementation
 *
 * Implements MCP communication via a child process's stdin/stdout.
 */

#define LOG_MODULE     LOG_MOD_MCP
#define LOG_MODULE_STR "mcp"
#include "../util/log.h"
#include "mcp_transport.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>

static int stdio_connect(MCPTransport *transport, const MCPServerConfig *config) {
    if (!transport || !config) {
        return -1;
    }

    if (config->type != MCP_SERVER_STDIO) {
        LOG_ERROR("STDIO transport: wrong server type");
        return -1;
    }

    if (!config->command) {
        LOG_ERROR("STDIO transport: no command specified");
        return -1;
    }

    StdioTransportData *data = transport->data;
    if (!data) {
        return -1;
    }

    int stdin_pipe[2], stdout_pipe[2];

    if (pipe(stdin_pipe) == -1) {
        LOG_ERROR("STDIO transport: failed to create stdin pipe: %s", strerror(errno));
        return -1;
    }

    if (pipe(stdout_pipe) == -1) {
        LOG_ERROR("STDIO transport: failed to create stdout pipe: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOG_ERROR("STDIO transport: failed to fork: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        /* Set environment variables */
        for (size_t i = 0; i < config->env_vars.count; i++) {
            setenv(config->env_vars.data[i].key,
                   config->env_vars.data[i].value, 1);
        }

        /* Build argv array */
        char **argv = malloc((config->args.count + 2) * sizeof(char *));
        if (!argv) {
            _exit(1);
        }

        argv[0] = config->command;
        for (size_t i = 0; i < config->args.count; i++) {
            argv[i + 1] = config->args.data[i];
        }
        argv[config->args.count + 1] = NULL;

        execv(config->command, argv);
        LOG_ERROR("STDIO transport: execv failed: %s", strerror(errno));
        _exit(1);
    }

    /* Parent process */
    data->process_id = pid;
    data->stdin_fd = stdin_pipe[1];
    data->stdout_fd = stdout_pipe[0];

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Non-blocking so reads don't hang if the server is slow to respond */
    int flags = fcntl(data->stdout_fd, F_GETFL, 0);
    fcntl(data->stdout_fd, F_SETFL, flags | O_NONBLOCK);

    transport->config = config;
    transport->connected = 1;

    LOG_INFO("STDIO transport: started process %d for %s", pid, config->name);
    return 0;
}

static int stdio_disconnect(MCPTransport *transport) {
    if (!transport || !transport->connected) {
        return 0;
    }

    StdioTransportData *data = transport->data;
    if (!data) {
        return 0;
    }

    if (data->stdin_fd > 0) {
        close(data->stdin_fd);
        data->stdin_fd = 0;
    }

    if (data->stdout_fd > 0) {
        close(data->stdout_fd);
        data->stdout_fd = 0;
    }

    if (data->process_id > 0) {
        kill(data->process_id, SIGTERM);
        int status;
        waitpid(data->process_id, &status, WNOHANG);
        data->process_id = 0;
    }

    transport->connected = 0;

    LOG_INFO("STDIO transport: disconnected");
    return 0;
}

static int stdio_send_request(MCPTransport *transport, const char *request, char **response) {
    if (!transport || !transport->connected || !request || !response) {
        return -1;
    }

    StdioTransportData *data = transport->data;
    if (!data || data->stdin_fd <= 0 || data->stdout_fd <= 0) {
        LOG_ERROR("STDIO transport: not connected");
        return -1;
    }

    *response = NULL;

    size_t request_len = strlen(request);
    ssize_t written = write(data->stdin_fd, request, request_len);
    if (written != (ssize_t)request_len) {
        LOG_ERROR("STDIO transport: failed to write request");
        return -1;
    }

    if (write(data->stdin_fd, "\n", 1) != 1) {
        LOG_ERROR("STDIO transport: failed to write newline");
        return -1;
    }

    /* Wait for response with timeout using select */
    fd_set read_fds;
    struct timeval timeout;
    int max_retries = 50;  /* 50 * 100ms = 5 seconds max */

    /* Dynamic buffer for response */
    size_t buf_size = 8192;
    size_t buf_used = 0;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        LOG_ERROR("STDIO transport: malloc failed");
        return -1;
    }

    int got_data = 0;
    for (int i = 0; i < max_retries; i++) {
        FD_ZERO(&read_fds);
        FD_SET(data->stdout_fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  /* 100ms */

        int select_result = select(data->stdout_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (select_result < 0) {
            if (errno == EINTR) {
                continue;  /* Interrupted, retry */
            }
            LOG_ERROR("STDIO transport: select failed: %s", strerror(errno));
            free(buffer);
            return -1;
        }

        if (select_result == 0) {
            if (got_data) {
                break;  /* Have data and no more coming */
            }
            continue;  /* Timeout, retry */
        }

        /* Data available - grow buffer if needed */
        if (buf_used + 4096 >= buf_size) {
            size_t new_size = buf_size * 2;
            char *new_buf = realloc(buffer, new_size);
            if (!new_buf) {
                LOG_ERROR("STDIO transport: realloc failed");
                free(buffer);
                return -1;
            }
            buffer = new_buf;
            buf_size = new_size;
        }

        ssize_t bytes_read = read(data->stdout_fd, buffer + buf_used, buf_size - buf_used - 1);
        if (bytes_read > 0) {
            buf_used += bytes_read;
            got_data = 1;
            /* Check if we have a complete JSON response (newline-terminated) */
            if (buf_used > 0 && buffer[buf_used - 1] == '\n') {
                break;
            }
            continue;  /* Keep reading */
        }

        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (got_data) {
                    break;  /* Have data and pipe would block */
                }
                continue;  /* No data yet, retry */
            }
            if (errno == EINTR) {
                continue;  /* Interrupted, retry */
            }
            LOG_ERROR("STDIO transport: read failed: %s", strerror(errno));
            free(buffer);
            return -1;
        }

        /* bytes_read == 0 means EOF */
        if (got_data) {
            break;  /* Have data before EOF */
        }
        LOG_ERROR("STDIO transport: EOF from server");
        free(buffer);
        return -1;
    }

    if (buf_used == 0) {
        LOG_ERROR("STDIO transport: timeout waiting for response");
        free(buffer);
        return -1;
    }

    buffer[buf_used] = '\0';
    *response = buffer;  /* Transfer ownership */

    return 0;
}

static void stdio_destroy(MCPTransport *transport) {
    if (!transport) {
        return;
    }

    stdio_disconnect(transport);
    free(transport->data);
    free(transport);
}

const MCPTransportOps mcp_transport_stdio_ops = {
    .connect = stdio_connect,
    .disconnect = stdio_disconnect,
    .send_request = stdio_send_request,
    .destroy = stdio_destroy
};
