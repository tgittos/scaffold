#include "oauth_callback_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define REQUEST_BUF_SIZE 4096

static const char *SUCCESS_HTML =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><body>"
    "<h1>Login Successful</h1>"
    "<p>You can close this tab and return to the terminal.</p>"
    "</body></html>";

static const char *ERROR_HTML =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><body>"
    "<h1>Login Failed</h1>"
    "<p>An error occurred during authentication. Please try again.</p>"
    "</body></html>";

/* Extract a query parameter value from a URL string */
static int extract_query_param(const char *url, const char *param,
                                char *out, size_t out_len) {
    if (!url || !param || !out || out_len == 0) return -1;

    size_t param_len = strlen(param);
    const char *search = url;

    while ((search = strstr(search, param)) != NULL) {
        /* Verify it's preceded by ? or & (not a substring of another param) */
        if (search > url) {
            char prev = *(search - 1);
            if (prev != '?' && prev != '&') {
                search += param_len;
                continue;
            }
        }

        /* Check for '=' after param name */
        if (search[param_len] != '=') {
            search += param_len;
            continue;
        }

        const char *value_start = search + param_len + 1;
        const char *value_end = value_start;
        while (*value_end && *value_end != '&' && *value_end != ' ' &&
               *value_end != '\r' && *value_end != '\n') {
            value_end++;
        }

        size_t value_len = (size_t)(value_end - value_start);
        if (value_len + 1 > out_len) return -1;

        memcpy(out, value_start, value_len);
        out[value_len] = '\0';
        return 0;
    }

    return -1;
}

int oauth_callback_server_wait(int port, int timeout_s, OAuthCallbackResult *result) {
    if (!result || port <= 0) return -1;

    memset(result, 0, sizeof(*result));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
        .sin_port = htons((uint16_t)port),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == EADDRINUSE) {
            fprintf(stderr, "Error: Port %d is already in use.\n"
                    "Another scaffold instance may be running, or another process is using this port.\n"
                    "Try: lsof -i :%d\n", port, port);
        }
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 1) < 0) {
        close(server_fd);
        return -1;
    }

    /* Wait for connection with timeout using select() */
    if (timeout_s > 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        struct timeval tv = { .tv_sec = timeout_s, .tv_usec = 0 };
        int sel = select(server_fd + 1, &readfds, NULL, NULL, &tv);
        if (sel <= 0) {
            close(server_fd);
            return -1; /* Timeout or error */
        }
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    close(server_fd);

    if (client_fd < 0) return -1;

    /* Read the HTTP request */
    char request[REQUEST_BUF_SIZE] = {0};
    ssize_t n = read(client_fd, request, sizeof(request) - 1);
    if (n <= 0) {
        close(client_fd);
        return -1;
    }

    /* Parse: GET /auth/callback?code=...&state=... HTTP/1.1 */
    if (strncmp(request, "GET ", 4) != 0) {
        write(client_fd, ERROR_HTML, strlen(ERROR_HTML));
        close(client_fd);
        return -1;
    }

    /* Check for error parameter */
    if (extract_query_param(request, "error", result->error, sizeof(result->error)) == 0) {
        result->success = 0;
        write(client_fd, ERROR_HTML, strlen(ERROR_HTML));
        close(client_fd);
        return 0;
    }

    /* Extract code and state */
    int got_code = extract_query_param(request, "code", result->code, sizeof(result->code));
    int got_state = extract_query_param(request, "state", result->state, sizeof(result->state));

    if (got_code == 0 && got_state == 0) {
        result->success = 1;
        write(client_fd, SUCCESS_HTML, strlen(SUCCESS_HTML));
    } else {
        result->success = 0;
        snprintf(result->error, sizeof(result->error), "missing code or state");
        write(client_fd, ERROR_HTML, strlen(ERROR_HTML));
    }

    close(client_fd);
    return 0;
}
