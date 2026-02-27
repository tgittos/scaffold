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

static const char *COMMON_CSS =
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{min-height:100vh;display:flex;align-items:center;justify-content:center;"
    "background:#FAF7F2;color:#1A1A18;font-family:system-ui,sans-serif;font-weight:300}"
    ".card{text-align:center;padding:3rem;animation:arrive .8s cubic-bezier(.16,1,.3,1) both}"
    "@keyframes arrive{from{opacity:0;transform:translateY(12px)}to{opacity:1;transform:translateY(0)}}"
    ".mark{width:72px;height:72px;margin:0 auto 2rem;border-radius:50%;"
    "display:flex;align-items:center;justify-content:center;"
    "animation:pop .5s .3s cubic-bezier(.34,1.56,.64,1) both}"
    "@keyframes pop{from{opacity:0;transform:scale(.5)}to{opacity:1;transform:scale(1)}}"
    ".mark svg{stroke:#FAF7F2;stroke-width:2.5;fill:none;"
    "stroke-linecap:round;stroke-linejoin:round}"
    "@keyframes draw{to{stroke-dashoffset:0}}"
    "h1{font-family:Georgia,serif;"
    "font-size:clamp(2rem,5vw,3.5rem);"
    "font-weight:400;letter-spacing:-.02em;line-height:1.1;margin-bottom:.75rem}"
    "p{font-size:1.05rem;color:#8B7355;letter-spacing:.01em}"
    ".brand{margin-top:3rem;font-size:.75rem;letter-spacing:.15em;"
    "text-transform:uppercase;color:#C4B99A}";

static const char *SUCCESS_BODY =
    ".mark{background:#C4632A}"
    ".mark svg{width:32px;height:32px}"
    ".mark svg path{stroke-dasharray:30;stroke-dashoffset:30;"
    "animation:draw .4s .6s ease forwards}"
    "</style></head><body>"
    "<div class='card'>"
    "<div class='mark'><svg viewBox='0 0 32 32'>"
    "<path d='M8 17l6 6 10-14'/></svg></div>"
    "<h1>You're in.</h1>"
    "<p>Close this tab and return to your terminal.</p>"
    "<div class='brand'>scaffold</div>"
    "</div></body></html>";

static const char *ERROR_BODY =
    ".mark{background:#B5483A}"
    ".mark svg{width:28px;height:28px}"
    ".mark svg line{stroke-dasharray:20;stroke-dashoffset:20;"
    "animation:draw .4s .6s ease forwards}"
    ".mark svg line+line{animation-delay:.75s}"
    "</style></head><body>"
    "<div class='card'>"
    "<div class='mark'><svg viewBox='0 0 32 32'>"
    "<line x1='10' y1='10' x2='22' y2='22'/>"
    "<line x1='22' y1='10' x2='10' y2='22'/></svg></div>"
    "<h1>That didn't work.</h1>"
    "<p>Something went wrong. Try logging in again from your terminal.</p>"
    "<div class='brand'>scaffold</div>"
    "</div></body></html>";

static const char *HTML_HEAD =
    "<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>scaffold</title>"
    "<style>";

static void send_html_response(int fd, const char *status_line, const char *body) {
    (void)write(fd, status_line, strlen(status_line));
    (void)write(fd, HTML_HEAD, strlen(HTML_HEAD));
    (void)write(fd, COMMON_CSS, strlen(COMMON_CSS));
    (void)write(fd, body, strlen(body));
}

#define SUCCESS_STATUS "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
#define ERROR_STATUS   "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"

/* Decode percent-encoded sequences (%XX) in-place */
static void percent_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            unsigned int hi = (unsigned char)r[1];
            unsigned int lo = (unsigned char)r[2];
            /* Convert hex chars to nibbles */
            if (hi >= '0' && hi <= '9') hi -= '0';
            else if (hi >= 'A' && hi <= 'F') hi = hi - 'A' + 10;
            else if (hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10;
            else { *w++ = *r++; continue; }
            if (lo >= '0' && lo <= '9') lo -= '0';
            else if (lo >= 'A' && lo <= 'F') lo = lo - 'A' + 10;
            else if (lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10;
            else { *w++ = *r++; continue; }
            *w++ = (char)((hi << 4) | lo);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' ';
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

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
        percent_decode(out);
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

    /* Reject connections from non-loopback addresses */
    if (client_addr.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
        close(client_fd);
        snprintf(result->error, sizeof(result->error), "non-loopback connection rejected");
        return -1;
    }

    /* Read the HTTP request */
    char request[REQUEST_BUF_SIZE] = {0};
    ssize_t n = read(client_fd, request, sizeof(request) - 1);
    if (n <= 0) {
        close(client_fd);
        return -1;
    }

    /* Parse: GET /auth/callback?code=...&state=... HTTP/1.1 */
    if (strncmp(request, "GET ", 4) != 0) {
        send_html_response(client_fd, ERROR_STATUS, ERROR_BODY);
        close(client_fd);
        return -1;
    }

    /* Check for error parameter */
    if (extract_query_param(request, "error", result->error, sizeof(result->error)) == 0) {
        result->success = 0;
        send_html_response(client_fd, ERROR_STATUS, ERROR_BODY);
        close(client_fd);
        return 0;
    }

    /* Extract code and state */
    int got_code = extract_query_param(request, "code", result->code, sizeof(result->code));
    int got_state = extract_query_param(request, "state", result->state, sizeof(result->state));

    if (got_code == 0 && got_state == 0) {
        result->success = 1;
        send_html_response(client_fd, SUCCESS_STATUS, SUCCESS_BODY);
    } else {
        result->success = 0;
        snprintf(result->error, sizeof(result->error), "missing code or state");
        send_html_response(client_fd, ERROR_STATUS, ERROR_BODY);
    }

    close(client_fd);
    return 0;
}
