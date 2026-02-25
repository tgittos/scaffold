#include "unity.h"
#include "auth/openai_oauth_provider.h"
#include "../mock_api_server.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define TEST_PORT 9879

static MockAPIServer g_server;
static MockAPIResponse g_responses[2];

static char *token_callback(const char *request_body, void *user_data) {
    (void)user_data;
    (void)request_body;
    return strdup(
        "{\"access_token\":\"test_at_123\","
        "\"refresh_token\":\"test_rt_456\","
        "\"token_type\":\"Bearer\","
        "\"expires_in\":3600}");
}

void setUp(void) {
    g_responses[0] = (MockAPIResponse){
        .endpoint = "/oauth/token",
        .method = "POST",
        .response_code = 200,
        .callback = token_callback,
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

void test_provider_name(void) {
    const OAuth2ProviderOps *ops = openai_oauth_provider_ops();
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_EQUAL_STRING("openai", ops->name);
}

void test_build_auth_url(void) {
    const OAuth2ProviderOps *ops = openai_oauth_provider_ops();
    char *url = ops->build_auth_url("test_client", "http://localhost:1455/auth/callback",
                                     "openid email", "state123", "challenge_abc");
    TEST_ASSERT_NOT_NULL(url);
    TEST_ASSERT_NOT_NULL(strstr(url, "response_type=code"));
    TEST_ASSERT_NOT_NULL(strstr(url, "client_id=test_client"));
    TEST_ASSERT_NOT_NULL(strstr(url, "state=state123"));
    TEST_ASSERT_NOT_NULL(strstr(url, "code_challenge=challenge_abc"));
    TEST_ASSERT_NOT_NULL(strstr(url, "code_challenge_method=S256"));
    TEST_ASSERT_NOT_NULL(strstr(url, "codex_cli_simplified_flow=true"));
    free(url);
}

void test_vtable_has_refresh(void) {
    const OAuth2ProviderOps *ops = openai_oauth_provider_ops();
    TEST_ASSERT_NOT_NULL(ops->exchange_code);
    TEST_ASSERT_NOT_NULL(ops->refresh_token);
    TEST_ASSERT_NULL(ops->revoke_token);
}

void test_null_safety(void) {
    const OAuth2ProviderOps *ops = openai_oauth_provider_ops();
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_NOT_NULL(ops->build_auth_url);
    TEST_ASSERT_NOT_NULL(ops->exchange_code);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_provider_name);
    RUN_TEST(test_build_auth_url);
    RUN_TEST(test_vtable_has_refresh);
    RUN_TEST(test_null_safety);
    return UNITY_END();
}
