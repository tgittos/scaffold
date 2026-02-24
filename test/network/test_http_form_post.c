#include "unity/unity.h"
#include "network/http_form_post.h"
#include "network/http_client.h"
#include "../mock_api_server.h"
#include <string.h>
#include <unistd.h>

#define TEST_PORT 9877

static MockAPIServer g_server;
static MockAPIResponse g_responses[1];
static char g_captured_body[2048];

static char *capture_callback(const char *request_body, void *user_data) {
    (void)user_data;
    if (request_body) {
        snprintf(g_captured_body, sizeof(g_captured_body), "%s", request_body);
    }
    return strdup("{\"access_token\":\"test_token\",\"token_type\":\"Bearer\",\"expires_in\":3600}");
}

void setUp(void) {
    memset(g_captured_body, 0, sizeof(g_captured_body));
    g_responses[0] = (MockAPIResponse){
        .endpoint = "/oauth/token",
        .method = "POST",
        .response_code = 200,
        .callback = capture_callback,
    };
    g_server = (MockAPIServer){
        .port = TEST_PORT,
        .responses = g_responses,
        .response_count = 1,
    };
    mock_api_server_start(&g_server);
    mock_api_server_wait_ready(&g_server, 2000);
}

void tearDown(void) {
    mock_api_server_stop(&g_server);
}

void test_form_post_basic(void) {
    FormField fields[] = {
        {"grant_type", "authorization_code"},
        {"code", "test_code_123"},
        {"client_id", "my_client"},
    };
    struct HTTPResponse response = {0};
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/oauth/token", TEST_PORT);

    int rc = http_form_post(url, fields, 3, &response);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(200, response.http_status);
    TEST_ASSERT_NOT_NULL(response.data);
    TEST_ASSERT_NOT_NULL(strstr(response.data, "access_token"));
    cleanup_response(&response);
}

void test_form_post_url_encodes_values(void) {
    FormField fields[] = {
        {"redirect_uri", "http://localhost:1455/auth/callback"},
        {"code", "abc 123+def"},
    };
    struct HTTPResponse response = {0};
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/oauth/token", TEST_PORT);

    int rc = http_form_post(url, fields, 2, &response);
    TEST_ASSERT_EQUAL_INT(0, rc);
    /* Values with special chars should be URL-encoded */
    TEST_ASSERT_NOT_NULL(strstr(g_captured_body, "redirect_uri="));
    /* The space should be encoded as + or %20 */
    TEST_ASSERT_NULL(strstr(g_captured_body, "abc 123"));
    cleanup_response(&response);
}

void test_form_post_null_params(void) {
    FormField fields[] = {{"key", "val"}};
    struct HTTPResponse response = {0};
    TEST_ASSERT_EQUAL_INT(-1, http_form_post(NULL, fields, 1, &response));
    TEST_ASSERT_EQUAL_INT(-1, http_form_post("http://x", NULL, 1, &response));
    TEST_ASSERT_EQUAL_INT(-1, http_form_post("http://x", fields, 0, &response));
    TEST_ASSERT_EQUAL_INT(-1, http_form_post("http://x", fields, 1, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_form_post_basic);
    RUN_TEST(test_form_post_url_encodes_values);
    RUN_TEST(test_form_post_null_params);
    return UNITY_END();
}
