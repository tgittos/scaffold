#include "unity.h"
#include "auth/jwt_decode.h"
#include <string.h>
#include <mbedtls/base64.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: build a fake JWT with a given payload JSON */
static char g_jwt[4096];

static void build_test_jwt(const char *payload_json) {
    /* base64url encode the payload */
    unsigned char b64[2048];
    size_t olen = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &olen,
                           (const unsigned char *)payload_json, strlen(payload_json));

    /* Convert standard base64 to base64url */
    for (size_t i = 0; i < olen; i++) {
        if (b64[i] == '+') b64[i] = '-';
        else if (b64[i] == '/') b64[i] = '_';
    }
    /* Remove padding */
    while (olen > 0 && b64[olen - 1] == '=') olen--;
    b64[olen] = '\0';

    /* Build JWT: header.payload.signature (header and sig are dummy) */
    snprintf(g_jwt, sizeof(g_jwt), "eyJhbGciOiJSUzI1NiJ9.%s.fakesig", (char *)b64);
}

void test_extract_nested_claim(void) {
    const char *payload = "{\"https://api.openai.com/auth\":{\"chatgpt_account_id\":\"acct_123\"}}";
    build_test_jwt(payload);

    char out[128] = {0};
    int rc = jwt_extract_nested_claim(g_jwt, "https://api.openai.com/auth",
                                       "chatgpt_account_id", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("acct_123", out);
}

void test_extract_simple_nested(void) {
    const char *payload = "{\"user\":{\"name\":\"Alice\",\"id\":\"u42\"}}";
    build_test_jwt(payload);

    char out[128] = {0};
    int rc = jwt_extract_nested_claim(g_jwt, "user", "id", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("u42", out);
}

void test_missing_parent_key(void) {
    const char *payload = "{\"other\":{\"key\":\"val\"}}";
    build_test_jwt(payload);

    char out[128] = {0};
    int rc = jwt_extract_nested_claim(g_jwt, "missing", "key", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_missing_child_key(void) {
    const char *payload = "{\"parent\":{\"other\":\"val\"}}";
    build_test_jwt(payload);

    char out[128] = {0};
    int rc = jwt_extract_nested_claim(g_jwt, "parent", "missing", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_malformed_jwt_no_dots(void) {
    char out[128] = {0};
    int rc = jwt_extract_nested_claim("nodots", "p", "c", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_malformed_jwt_one_dot(void) {
    char out[128] = {0};
    int rc = jwt_extract_nested_claim("one.dot", "p", "c", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_null_params(void) {
    char out[128] = {0};
    TEST_ASSERT_EQUAL_INT(-1, jwt_extract_nested_claim(NULL, "p", "c", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, jwt_extract_nested_claim("a.b.c", NULL, "c", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, jwt_extract_nested_claim("a.b.c", "p", NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, jwt_extract_nested_claim("a.b.c", "p", "c", NULL, 10));
    TEST_ASSERT_EQUAL_INT(-1, jwt_extract_nested_claim("a.b.c", "p", "c", out, 0));
}

void test_buffer_too_small(void) {
    const char *payload = "{\"p\":{\"c\":\"long_value_that_wont_fit\"}}";
    build_test_jwt(payload);

    char out[5] = {0};
    int rc = jwt_extract_nested_claim(g_jwt, "p", "c", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_non_string_child(void) {
    const char *payload = "{\"p\":{\"c\":42}}";
    build_test_jwt(payload);

    char out[128] = {0};
    int rc = jwt_extract_nested_claim(g_jwt, "p", "c", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_extract_nested_claim);
    RUN_TEST(test_extract_simple_nested);
    RUN_TEST(test_missing_parent_key);
    RUN_TEST(test_missing_child_key);
    RUN_TEST(test_malformed_jwt_no_dots);
    RUN_TEST(test_malformed_jwt_one_dot);
    RUN_TEST(test_null_params);
    RUN_TEST(test_buffer_too_small);
    RUN_TEST(test_non_string_child);
    return UNITY_END();
}
