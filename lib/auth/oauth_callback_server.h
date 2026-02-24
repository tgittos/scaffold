#ifndef OAUTH_CALLBACK_SERVER_H
#define OAUTH_CALLBACK_SERVER_H

typedef struct {
    char code[512];
    char state[128];
    char error[256];
    int success;
} OAuthCallbackResult;

/*
 * Wait for a single OAuth callback on localhost.
 * Listens on 127.0.0.1:port, accepts one connection, parses
 * GET /auth/callback?code=...&state=... and returns the result.
 * Responds with an HTML success/error page, then closes the socket.
 *
 * @param port      Port to listen on (e.g. 1455)
 * @param timeout_s Seconds to wait before giving up (0 = no timeout)
 * @param result    Output: extracted code, state, and success flag
 * @return 0 on success, -1 on error/timeout
 */
int oauth_callback_server_wait(int port, int timeout_s, OAuthCallbackResult *result);

#endif /* OAUTH_CALLBACK_SERVER_H */
