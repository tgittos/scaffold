#include "unity.h"
#include "auth/oauth_callback_server.h"
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TEST_PORT 9878

void setUp(void) {}
void tearDown(void) {}

/* Helper: connect to localhost:port and send a GET request */
static void *send_callback_request(void *arg) {
    const char *request = (const char *)arg;
    usleep(100000); /* 100ms delay for server to start listening */

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
        .sin_port = htons(TEST_PORT),
    };

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    write(fd, request, strlen(request));

    /* Read and discard response */
    char buf[4096];
    read(fd, buf, sizeof(buf));

    close(fd);
    return NULL;
}

/* Helper: connect to a specific port and send a GET request */
static int g_custom_port = 0;
static void *send_callback_custom_port(void *arg) {
    const char *request = (const char *)arg;
    usleep(100000);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
        .sin_port = htons(g_custom_port),
    };

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    write(fd, request, strlen(request));
    char buf[4096];
    read(fd, buf, sizeof(buf));
    close(fd);
    return NULL;
}

void test_callback_success(void) {
    const char *request = "GET /auth/callback?code=abc123&state=xyz789 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    pthread_t tid;
    pthread_create(&tid, NULL, send_callback_request, (void *)request);

    OAuthCallbackResult result = {0};
    int rc = oauth_callback_server_wait(TEST_PORT, 5, &result);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_EQUAL_STRING("abc123", result.code);
    TEST_ASSERT_EQUAL_STRING("xyz789", result.state);

    pthread_join(tid, NULL);
}

void test_callback_with_error(void) {
    g_custom_port = TEST_PORT + 1;
    const char *request = "GET /auth/callback?error=access_denied HTTP/1.1\r\nHost: localhost\r\n\r\n";
    pthread_t tid;
    pthread_create(&tid, NULL, send_callback_custom_port, (void *)request);

    OAuthCallbackResult result = {0};
    int rc = oauth_callback_server_wait(TEST_PORT + 1, 5, &result);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_EQUAL_STRING("access_denied", result.error);

    pthread_join(tid, NULL);
}

void test_callback_missing_params(void) {
    g_custom_port = TEST_PORT + 2;
    const char *request = "GET /auth/callback?code=onlycode HTTP/1.1\r\nHost: localhost\r\n\r\n";
    pthread_t tid;
    pthread_create(&tid, NULL, send_callback_custom_port, (void *)request);

    OAuthCallbackResult result = {0};
    int rc = oauth_callback_server_wait(g_custom_port, 5, &result);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(0, result.success);

    pthread_join(tid, NULL);
}

void test_callback_timeout(void) {
    OAuthCallbackResult result = {0};
    int rc = oauth_callback_server_wait(TEST_PORT + 3, 1, &result);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_callback_null_result(void) {
    TEST_ASSERT_EQUAL_INT(-1, oauth_callback_server_wait(TEST_PORT + 4, 1, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_callback_success);
    RUN_TEST(test_callback_with_error);
    RUN_TEST(test_callback_missing_params);
    RUN_TEST(test_callback_timeout);
    RUN_TEST(test_callback_null_result);
    return UNITY_END();
}
