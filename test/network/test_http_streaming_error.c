#include "../unity/unity.h"
#include "network/http_client.h"
#include "network/api_error.h"
#include "../mock_api_server.h"
#include <string.h>
#include <unistd.h>

#define TEST_PORT 9880

static MockAPIServer g_server;
static MockAPIResponse g_responses[2];

static size_t noop_stream_callback(const char *data, size_t size, void *user_data)
{
    (void)data;
    (void)user_data;
    return size;
}

void setUp(void)
{
    clear_last_api_error();
    memset(g_responses, 0, sizeof(g_responses));
    memset(&g_server, 0, sizeof(g_server));
}

void tearDown(void)
{
    if (g_server.is_running) {
        mock_api_server_stop(&g_server);
    }
}

/*
 * test_streaming_404_reports_model
 *
 * The mock server returns a 404 with a provider-style JSON error body.
 * After the streaming call fails, api_error_user_message() should include
 * the HTTP status code and the provider's error.message field.
 */
void test_streaming_404_reports_model(void)
{
    g_responses[0] = (MockAPIResponse){
        .endpoint = "/v1/chat/completions",
        .method   = "POST",
        .response_code  = 404,
        .response_body  = "{\"error\":{\"message\":\"No endpoints found for nonexistent/model\"}}",
    };
    g_server = (MockAPIServer){
        .port           = TEST_PORT,
        .responses      = g_responses,
        .response_count = 1,
    };
    mock_api_server_start(&g_server);
    mock_api_server_wait_ready(&g_server, 2000);

    struct StreamingHTTPConfig cfg = DEFAULT_STREAMING_HTTP_CONFIG;
    cfg.stream_callback = noop_stream_callback;

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/v1/chat/completions", TEST_PORT);

    int rc = http_post_streaming(url, "{}", NULL, &cfg);
    TEST_ASSERT_NOT_EQUAL(0, rc);

    APIError err;
    get_last_api_error(&err);

    const char *msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "404") != NULL);
    TEST_ASSERT_TRUE(strstr(msg, "No endpoints found") != NULL);
}

/*
 * test_streaming_302_is_failure
 *
 * A 302 redirect from an LLM endpoint is a configuration error — the
 * streaming code must NOT treat it as success.
 */
void test_streaming_302_is_failure(void)
{
    g_responses[0] = (MockAPIResponse){
        .endpoint = "/v1/chat/completions",
        .method   = "POST",
        .response_code  = 302,
        .response_body  = "",
    };
    g_server = (MockAPIServer){
        .port           = TEST_PORT + 1,  /* 9881 — avoids collisions with oauth tests (9878/9879) and test_http_form_post (9877) */
        .responses      = g_responses,
        .response_count = 1,
    };
    mock_api_server_start(&g_server);
    mock_api_server_wait_ready(&g_server, 2000);

    struct StreamingHTTPConfig cfg = DEFAULT_STREAMING_HTTP_CONFIG;
    cfg.stream_callback = noop_stream_callback;

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/v1/chat/completions", TEST_PORT + 1);

    int rc = http_post_streaming(url, "{}", NULL, &cfg);
    TEST_ASSERT_NOT_EQUAL(0, rc);

    APIError err;
    get_last_api_error(&err);
    TEST_ASSERT_EQUAL(302, err.http_status);

    const char *msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "302") != NULL);
    TEST_ASSERT_TRUE(strstr(msg, "redirect") != NULL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_streaming_404_reports_model);
    RUN_TEST(test_streaming_302_is_failure);
    return UNITY_END();
}
