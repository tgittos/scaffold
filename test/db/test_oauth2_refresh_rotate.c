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

/* Non-rotating refresh: returns new access token but leaves refresh token unchanged */
static OAuth2Error mock_refresh_non_rotating(const char *client_id, const char *client_secret,
                                              const char *refresh_token_in,
                                              char *access_token, size_t at_len,
                                              char *new_refresh_token, size_t rt_len,
                                              int64_t *expires_in) {
    (void)client_id; (void)client_secret; (void)refresh_token_in;
    mock_refresh_called++;
    snprintf(access_token, at_len, "non_rotating_access");
    if (new_refresh_token && rt_len > 0)
        new_refresh_token[0] = '\0';
    *expires_in = 3600;
    return OAUTH2_OK;
}

/* Rotating provider — like OpenAI: returns a new refresh token each time */
static const OAuth2ProviderOps rotating_ops = {
    .name = "rotating",
    .build_auth_url = mock_build_auth_url,
    .exchange_code = mock_exchange_code,
    .refresh_token = mock_refresh_rotate,
};

/* Non-rotating provider: returns new access token, preserves refresh token */
static const OAuth2ProviderOps non_rotating_ops = {
    .name = "non_rotating",
    .build_auth_url = mock_build_auth_url,
    .exchange_code = mock_exchange_code,
    .refresh_token = mock_refresh_non_rotating,
};

/* Provider with no refresh capability */
static const OAuth2ProviderOps no_refresh_ops = {
    .name = "no_refresh",
    .build_auth_url = mock_build_auth_url,
    .exchange_code = mock_exchange_code,
    .refresh_token = NULL,
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

void test_non_rotating_preserves_refresh_token(void) {
    oauth2_store_register_provider(g_store, &non_rotating_ops);
    store_expired_token("non_rotating");

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "non_rotating",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("non_rotating_access", result.access_token);
    TEST_ASSERT_EQUAL_INT(0, mock_rotate_called);
    TEST_ASSERT_EQUAL_INT(1, mock_refresh_called);

    /* Verify original refresh token is preserved in DB */
    sqlite3 *db = NULL;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(g_test_db_path, &db));

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db,
        "SELECT refresh_token FROM oauth2_tokens "
        "WHERE provider='non_rotating' AND account_id='user@test.com'",
        -1, &stmt, NULL);
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));

    const char *stored_rt = (const char *)sqlite3_column_text(stmt, 0);
    TEST_ASSERT_NOT_NULL(stored_rt);
    TEST_ASSERT_EQUAL_STRING("initial_refresh", stored_rt);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void test_refresh_failure_returns_expired(void) {
    oauth2_store_register_provider(g_store, &rotating_ops);
    store_expired_token("rotating");

    mock_rotate_result = OAUTH2_ERROR_NETWORK;

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "rotating",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_EXPIRED, err);
}

void test_no_refresh_capability_returns_expired(void) {
    oauth2_store_register_provider(g_store, &no_refresh_ops);
    store_expired_token("no_refresh");

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "no_refresh",
                                                     "user@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_PROVIDER, err);
}

/* ========================================================================= */
/* Runner                                                                    */
/* ========================================================================= */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rotate_stores_new_refresh_token);
    RUN_TEST(test_non_rotating_preserves_refresh_token);
    RUN_TEST(test_refresh_failure_returns_expired);
    RUN_TEST(test_no_refresh_capability_returns_expired);
    return UNITY_END();
}
