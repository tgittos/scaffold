#include "unity.h"
#include "db/oauth2_store.h"
#include "../test_fs_utils.h"

#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static oauth2_store_t *g_store = NULL;
static char g_test_db_path[256];

/* ========================================================================= */
/* Mock providers                                                            */
/* ========================================================================= */

static int mock_rotate_called = 0;
static int mock_refresh_called = 0;
static OAuth2Error mock_rotate_result = OAUTH2_OK;

static char *mock_build_auth_url(const char *client_id, const char *redirect_uri,
                                 const char *scope, const char *state,
                                 const char *code_challenge) {
    char *url = malloc(512);
    if (!url) return NULL;
    snprintf(url, 512, "https://mock.example.com/auth?client_id=%s&redirect_uri=%s"
             "&scope=%s&state=%s&code_challenge=%s",
             client_id, redirect_uri ? redirect_uri : "",
             scope ? scope : "", state, code_challenge);
    return url;
}

static OAuth2Error mock_exchange_code(const char *client_id, const char *client_secret,
                                      const char *redirect_uri, const char *code,
                                      const char *code_verifier,
                                      char *access_token, size_t at_len,
                                      char *refresh_token, size_t rt_len,
                                      int64_t *expires_in) {
    (void)client_id; (void)client_secret; (void)redirect_uri;
    (void)code; (void)code_verifier;
    snprintf(access_token, at_len, "initial_access");
    snprintf(refresh_token, rt_len, "initial_refresh");
    *expires_in = 1; /* Expire immediately to trigger refresh */
    return OAUTH2_OK;
}

/* Rotating refresh: returns new access + new refresh token */
static OAuth2Error mock_refresh_rotate(const char *client_id, const char *client_secret,
                                        const char *refresh_token_in,
                                        char *access_token, size_t at_len,
                                        char *new_refresh_token, size_t rt_len,
                                        int64_t *expires_in) {
    (void)client_id; (void)client_secret; (void)refresh_token_in;
    mock_rotate_called++;
    if (mock_rotate_result != OAUTH2_OK) return mock_rotate_result;
    snprintf(access_token, at_len, "rotated_access");
    snprintf(new_refresh_token, rt_len, "rotated_refresh");
    *expires_in = 3600;
    return OAUTH2_OK;
}

/* Non-rotating refresh (fallback) */
static OAuth2Error mock_refresh_token(const char *client_id, const char *client_secret,
                                       const char *refresh_token_in,
                                       char *access_token, size_t at_len,
                                       int64_t *expires_in) {
    (void)client_id; (void)client_secret; (void)refresh_token_in;
    mock_refresh_called++;
    snprintf(access_token, at_len, "fallback_access");
    *expires_in = 3600;
    return OAUTH2_OK;
}

/* Provider with BOTH rotate and fallback refresh */
static const OAuth2ProviderOps rotating_ops = {
    .name = "rotating",
    .build_auth_url = mock_build_auth_url,
    .exchange_code = mock_exchange_code,
    .refresh_token = mock_refresh_token,
    .refresh_token_rotate = mock_refresh_rotate,
};

/* Provider with ONLY rotate (no fallback) — like OpenAI */
static const OAuth2ProviderOps rotate_only_ops = {
    .name = "rotate_only",
    .build_auth_url = mock_build_auth_url,
    .exchange_code = mock_exchange_code,
    .refresh_token = NULL,
    .refresh_token_rotate = mock_refresh_rotate,
};

/* Provider with ONLY non-rotating refresh (backward compat) */
static const OAuth2ProviderOps legacy_ops = {
    .name = "legacy",
    .build_auth_url = mock_build_auth_url,
    .exchange_code = mock_exchange_code,
    .refresh_token = mock_refresh_token,
    .refresh_token_rotate = NULL,
};

/* ========================================================================= */
/* setUp / tearDown                                                          */
/* ========================================================================= */

void setUp(void) {
    snprintf(g_test_db_path, sizeof(g_test_db_path),
             "/tmp/test_oauth2_rotate_%d.db", getpid());
    unlink_sqlite_db(g_test_db_path);

    OAuth2Config cfg = {
        .db_path = g_test_db_path,
        .redirect_uri = "http://localhost:1455/auth/callback",
    };
    g_store = oauth2_store_create(&cfg);

    mock_rotate_called = 0;
    mock_refresh_called = 0;
    mock_rotate_result = OAUTH2_OK;
}

void tearDown(void) {
    if (g_store) {
        oauth2_store_destroy(g_store);
        g_store = NULL;
    }
    unlink_sqlite_db(g_test_db_path);
}

/* Helper: store a token that's already expired */
static void store_expired_token(const char *provider_name) {
    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, provider_name, "client", "scope", &req);
    oauth2_store_complete_auth(g_store, req.state, "code",
                                "client", "secret", "user@test.com");
}

/* ========================================================================= */
/* Tests                                                                     */
/* ========================================================================= */

void test_rotate_stores_new_refresh_token(void) {
    oauth2_store_register_provider(g_store, &rotating_ops);
    store_expired_token("rotating");

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "rotating",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("rotated_access", result.access_token);
    TEST_ASSERT_EQUAL_INT(1, mock_rotate_called);
    TEST_ASSERT_EQUAL_INT(0, mock_refresh_called);

    /* Verify the rotated refresh token is stored in DB */
    sqlite3 *db = NULL;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(g_test_db_path, &db));

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db,
        "SELECT refresh_token FROM oauth2_tokens "
        "WHERE provider='rotating' AND account_id='user@test.com'",
        -1, &stmt, NULL);
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));

    const char *stored_rt = (const char *)sqlite3_column_text(stmt, 0);
    TEST_ASSERT_NOT_NULL(stored_rt);
    TEST_ASSERT_EQUAL_STRING("rotated_refresh", stored_rt);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void test_rotate_fallback_to_legacy(void) {
    oauth2_store_register_provider(g_store, &rotating_ops);
    store_expired_token("rotating");

    /* Make rotate fail — should fall back to legacy refresh */
    mock_rotate_result = OAUTH2_ERROR_NETWORK;

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "rotating",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("fallback_access", result.access_token);
    TEST_ASSERT_EQUAL_INT(1, mock_rotate_called);
    TEST_ASSERT_EQUAL_INT(1, mock_refresh_called);
}

void test_rotate_only_no_fallback(void) {
    oauth2_store_register_provider(g_store, &rotate_only_ops);
    store_expired_token("rotate_only");

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "rotate_only",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("rotated_access", result.access_token);
    TEST_ASSERT_EQUAL_INT(1, mock_rotate_called);
}

void test_rotate_only_failure_no_fallback(void) {
    oauth2_store_register_provider(g_store, &rotate_only_ops);
    store_expired_token("rotate_only");

    mock_rotate_result = OAUTH2_ERROR_NETWORK;

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "rotate_only",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_EXPIRED, err);
}

void test_legacy_backward_compat(void) {
    oauth2_store_register_provider(g_store, &legacy_ops);
    store_expired_token("legacy");

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "legacy",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("fallback_access", result.access_token);
    TEST_ASSERT_EQUAL_INT(0, mock_rotate_called);
    TEST_ASSERT_EQUAL_INT(1, mock_refresh_called);
}

/* ========================================================================= */
/* Runner                                                                    */
/* ========================================================================= */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rotate_stores_new_refresh_token);
    RUN_TEST(test_rotate_fallback_to_legacy);
    RUN_TEST(test_rotate_only_no_fallback);
    RUN_TEST(test_rotate_only_failure_no_fallback);
    RUN_TEST(test_legacy_backward_compat);
    return UNITY_END();
}
