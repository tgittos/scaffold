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
/* Mock provider                                                             */
/* ========================================================================= */

static int mock_exchange_called = 0;
static int mock_refresh_called = 0;
static OAuth2Error mock_exchange_result = OAUTH2_OK;
static OAuth2Error mock_refresh_result = OAUTH2_OK;
static int64_t mock_expires_in = 3600;

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
    mock_exchange_called++;
    if (mock_exchange_result != OAUTH2_OK) return mock_exchange_result;
    snprintf(access_token, at_len, "mock_access_token");
    snprintf(refresh_token, rt_len, "mock_refresh_token");
    *expires_in = mock_expires_in;
    return OAUTH2_OK;
}

static OAuth2Error mock_refresh_token(const char *client_id, const char *client_secret,
                                       const char *refresh_token_in,
                                       char *access_token, size_t at_len,
                                       char *new_refresh_token, size_t rt_len,
                                       int64_t *expires_in) {
    (void)client_id; (void)client_secret; (void)refresh_token_in;
    mock_refresh_called++;
    if (mock_refresh_result != OAUTH2_OK) return mock_refresh_result;
    snprintf(access_token, at_len, "refreshed_access_token");
    if (new_refresh_token && rt_len > 0)
        new_refresh_token[0] = '\0';
    *expires_in = 3600;
    return OAUTH2_OK;
}

static const OAuth2ProviderOps mock_ops = {
    .name = "mock",
    .build_auth_url = mock_build_auth_url,
    .exchange_code = mock_exchange_code,
    .refresh_token = mock_refresh_token,
};

/* ========================================================================= */
/* setUp / tearDown                                                          */
/* ========================================================================= */

void setUp(void) {
    snprintf(g_test_db_path, sizeof(g_test_db_path),
             "/tmp/test_oauth2_store_%d.db", getpid());
    unlink_sqlite_db(g_test_db_path);

    OAuth2Config cfg = {
        .db_path = g_test_db_path,
        .redirect_uri = "http://localhost:8080/api/v1/oauth2/callback",
    };
    g_store = oauth2_store_create(&cfg);

    mock_exchange_called = 0;
    mock_refresh_called = 0;
    mock_exchange_result = OAUTH2_OK;
    mock_refresh_result = OAUTH2_OK;
    mock_expires_in = 3600;
}

void tearDown(void) {
    if (g_store) {
        oauth2_store_destroy(g_store);
        g_store = NULL;
    }
    unlink_sqlite_db(g_test_db_path);
}

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

void test_create_destroy(void) {
    TEST_ASSERT_NOT_NULL(g_store);
}

void test_create_null_config(void) {
    TEST_ASSERT_NULL(oauth2_store_create(NULL));
}

void test_create_null_db_path(void) {
    OAuth2Config cfg = { .db_path = NULL };
    TEST_ASSERT_NULL(oauth2_store_create(&cfg));
}

/* ========================================================================= */
/* Provider registry                                                         */
/* ========================================================================= */

void test_register_provider(void) {
    TEST_ASSERT_EQUAL_INT(0, oauth2_store_register_provider(g_store, &mock_ops));
}

void test_register_duplicate_provider(void) {
    TEST_ASSERT_EQUAL_INT(0, oauth2_store_register_provider(g_store, &mock_ops));
    TEST_ASSERT_EQUAL_INT(-1, oauth2_store_register_provider(g_store, &mock_ops));
}

void test_register_null_safety(void) {
    TEST_ASSERT_EQUAL_INT(-1, oauth2_store_register_provider(NULL, &mock_ops));
    TEST_ASSERT_EQUAL_INT(-1, oauth2_store_register_provider(g_store, NULL));
}

/* ========================================================================= */
/* Begin auth                                                                */
/* ========================================================================= */

void test_begin_auth(void) {
    oauth2_store_register_provider(g_store, &mock_ops);

    OAuth2AuthRequest req = {0};
    OAuth2Error err = oauth2_store_begin_auth(g_store, "mock", "client123",
                                               "email", &req);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(req.auth_url));
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(req.state));
    TEST_ASSERT_NOT_NULL(strstr(req.auth_url, "client_id=client123"));
    TEST_ASSERT_NOT_NULL(strstr(req.auth_url, "code_challenge="));
}

void test_begin_auth_unknown_provider(void) {
    OAuth2AuthRequest req = {0};
    OAuth2Error err = oauth2_store_begin_auth(g_store, "unknown", "client",
                                               "scope", &req);
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_PROVIDER, err);
}

void test_begin_auth_null_safety(void) {
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_INVALID,
        oauth2_store_begin_auth(NULL, "mock", "client", "scope", NULL));
}

/* ========================================================================= */
/* Complete auth                                                             */
/* ========================================================================= */

void test_complete_auth_flow(void) {
    oauth2_store_register_provider(g_store, &mock_ops);

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, "mock", "client123", "email", &req);

    OAuth2Error err = oauth2_store_complete_auth(g_store, req.state, "auth_code",
                                                  "client123", "secret",
                                                  "user@test.com");
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_INT(1, mock_exchange_called);
    TEST_ASSERT_TRUE(oauth2_store_has_token(g_store, "mock", "user@test.com"));
}

void test_complete_auth_invalid_state(void) {
    oauth2_store_register_provider(g_store, &mock_ops);

    OAuth2Error err = oauth2_store_complete_auth(g_store, "bad_state", "code",
                                                  "client", "secret",
                                                  "user@test.com");
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_NOT_FOUND, err);
}

void test_complete_auth_exchange_failure(void) {
    oauth2_store_register_provider(g_store, &mock_ops);
    mock_exchange_result = OAUTH2_ERROR_NETWORK;

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, "mock", "client123", "email", &req);

    OAuth2Error err = oauth2_store_complete_auth(g_store, req.state, "code",
                                                  "client123", "secret",
                                                  "user@test.com");
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_NETWORK, err);
    TEST_ASSERT_FALSE(oauth2_store_has_token(g_store, "mock", "user@test.com"));
}

/* ========================================================================= */
/* Token access                                                              */
/* ========================================================================= */

void test_get_access_token(void) {
    oauth2_store_register_provider(g_store, &mock_ops);

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(g_store, req.state, "code",
                                "client123", "secret", "user@test.com");

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "mock",
                                                     "user@test.com", "client123",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("mock_access_token", result.access_token);
    TEST_ASSERT_GREATER_THAN(0, result.expires_at);
}

void test_get_access_token_not_found(void) {
    oauth2_store_register_provider(g_store, &mock_ops);

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "mock",
                                                     "nobody@test.com", "client",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_NOT_FOUND, err);
}

void test_get_access_token_auto_refresh(void) {
    oauth2_store_register_provider(g_store, &mock_ops);
    mock_expires_in = 1;

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(g_store, req.state, "code",
                                "client123", "secret", "user@test.com");

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "mock",
                                                     "user@test.com", "client123",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("refreshed_access_token", result.access_token);
    TEST_ASSERT_EQUAL_INT(1, mock_refresh_called);
}

void test_get_access_token_refresh_failure(void) {
    oauth2_store_register_provider(g_store, &mock_ops);
    mock_expires_in = 1;

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(g_store, req.state, "code",
                                "client123", "secret", "user@test.com");

    mock_refresh_result = OAUTH2_ERROR_NETWORK;

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "mock",
                                                     "user@test.com", "client123",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_EXPIRED, err);
}

/* ========================================================================= */
/* Revoke                                                                    */
/* ========================================================================= */

void test_revoke_token(void) {
    oauth2_store_register_provider(g_store, &mock_ops);

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(g_store, req.state, "code",
                                "client123", "secret", "user@test.com");
    TEST_ASSERT_TRUE(oauth2_store_has_token(g_store, "mock", "user@test.com"));

    OAuth2Error err = oauth2_store_revoke_token(g_store, "mock", "user@test.com");
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_FALSE(oauth2_store_has_token(g_store, "mock", "user@test.com"));
}

/* ========================================================================= */
/* Cleanup                                                                   */
/* ========================================================================= */

void test_cleanup(void) {
    oauth2_store_register_provider(g_store, &mock_ops);
    mock_expires_in = -1;

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(g_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(g_store, req.state, "code",
                                "client123", "secret", "user@test.com");

    oauth2_store_expire_pending(g_store);
    TEST_ASSERT_TRUE(oauth2_store_has_token(g_store, "mock", "user@test.com"));
}

void test_has_token_false_when_empty(void) {
    TEST_ASSERT_FALSE(oauth2_store_has_token(g_store, "mock", "user@test.com"));
}

/* ========================================================================= */
/* Encryption                                                                */
/* ========================================================================= */

#define TEST_ENCRYPTION_KEY "test-secret-key-for-oauth2-encryption!"

void test_encrypted_token_roundtrip(void) {
    char enc_db[256];
    snprintf(enc_db, sizeof(enc_db), "/tmp/test_oauth2_enc_%d.db", getpid());
    unlink_sqlite_db(enc_db);

    OAuth2Config cfg = {
        .db_path = enc_db,
        .redirect_uri = "http://localhost:8080/api/v1/oauth2/callback",
        .encryption_key = (const unsigned char *)TEST_ENCRYPTION_KEY,
        .encryption_key_len = strlen(TEST_ENCRYPTION_KEY),
    };
    oauth2_store_t *enc_store = oauth2_store_create(&cfg);
    TEST_ASSERT_NOT_NULL(enc_store);
    oauth2_store_register_provider(enc_store, &mock_ops);

    OAuth2AuthRequest req = {0};
    OAuth2Error err = oauth2_store_begin_auth(enc_store, "mock", "client123",
                                               "email", &req);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);

    err = oauth2_store_complete_auth(enc_store, req.state, "code",
                                      "client123", "secret", "user@test.com");
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);

    OAuth2TokenResult result = {0};
    err = oauth2_store_get_access_token(enc_store, "mock", "user@test.com",
                                         "client123", "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("mock_access_token", result.access_token);

    oauth2_store_destroy(enc_store);
    unlink_sqlite_db(enc_db);
}

void test_encrypted_token_not_plaintext_in_db(void) {
    char enc_db[256];
    snprintf(enc_db, sizeof(enc_db), "/tmp/test_oauth2_enc2_%d.db", getpid());
    unlink_sqlite_db(enc_db);

    OAuth2Config cfg = {
        .db_path = enc_db,
        .redirect_uri = "http://localhost:8080/api/v1/oauth2/callback",
        .encryption_key = (const unsigned char *)TEST_ENCRYPTION_KEY,
        .encryption_key_len = strlen(TEST_ENCRYPTION_KEY),
    };
    oauth2_store_t *enc_store = oauth2_store_create(&cfg);
    TEST_ASSERT_NOT_NULL(enc_store);
    oauth2_store_register_provider(enc_store, &mock_ops);

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(enc_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(enc_store, req.state, "code",
                                "client123", "secret", "user@test.com");

    oauth2_store_destroy(enc_store);

    /* Open DB directly to verify tokens are not stored in plaintext */
    sqlite3 *db = NULL;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(enc_db, &db));

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db,
        "SELECT access_token, refresh_token FROM oauth2_tokens "
        "WHERE provider='mock' AND account_id='user@test.com'",
        -1, &stmt, NULL);
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));

    const char *raw_at = (const char *)sqlite3_column_text(stmt, 0);
    const char *raw_rt = (const char *)sqlite3_column_text(stmt, 1);

    TEST_ASSERT_NOT_NULL(raw_at);
    TEST_ASSERT_NOT_NULL(raw_rt);
    TEST_ASSERT_FALSE(strcmp(raw_at, "mock_access_token") == 0);
    TEST_ASSERT_FALSE(strcmp(raw_rt, "mock_refresh_token") == 0);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    unlink_sqlite_db(enc_db);
}

void test_plaintext_in_db_fails_with_encryption(void) {
    char enc_db[256];
    snprintf(enc_db, sizeof(enc_db), "/tmp/test_oauth2_enc3_%d.db", getpid());
    unlink_sqlite_db(enc_db);

    /* First: store token without encryption */
    OAuth2Config cfg_plain = {
        .db_path = enc_db,
        .redirect_uri = "http://localhost:8080/api/v1/oauth2/callback",
    };
    oauth2_store_t *plain_store = oauth2_store_create(&cfg_plain);
    TEST_ASSERT_NOT_NULL(plain_store);
    oauth2_store_register_provider(plain_store, &mock_ops);

    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(plain_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(plain_store, req.state, "code",
                                "client123", "secret", "user@test.com");
    oauth2_store_destroy(plain_store);

    /* Re-open with encryption — plaintext token should fail to decrypt */
    OAuth2Config cfg_enc = {
        .db_path = enc_db,
        .redirect_uri = "http://localhost:8080/api/v1/oauth2/callback",
        .encryption_key = (const unsigned char *)TEST_ENCRYPTION_KEY,
        .encryption_key_len = strlen(TEST_ENCRYPTION_KEY),
    };
    oauth2_store_t *enc_store = oauth2_store_create(&cfg_enc);
    TEST_ASSERT_NOT_NULL(enc_store);
    oauth2_store_register_provider(enc_store, &mock_ops);

    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(enc_store, "mock",
                                                     "user@test.com", "client123",
                                                     "secret", &result);
    TEST_ASSERT_NOT_EQUAL(OAUTH2_OK, err);

    oauth2_store_destroy(enc_store);
    unlink_sqlite_db(enc_db);
}

/* ========================================================================= */
/* NULL safety                                                               */
/* ========================================================================= */

void test_null_safety(void) {
    TEST_ASSERT_FALSE(oauth2_store_has_token(NULL, "mock", "user"));
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_INVALID,
        oauth2_store_revoke_token(NULL, "mock", "user"));
    oauth2_store_expire_pending(NULL);
    oauth2_store_destroy(NULL);
}

/* ========================================================================= */
/* Provider validation                                                       */
/* ========================================================================= */

void test_register_provider_null_build_auth_url(void) {
    static const OAuth2ProviderOps bad_ops = {
        .name = "bad",
        .build_auth_url = NULL,
        .exchange_code = mock_exchange_code,
        .refresh_token = mock_refresh_token,
    };
    TEST_ASSERT_EQUAL_INT(-1, oauth2_store_register_provider(g_store, &bad_ops));
}

void test_register_provider_null_exchange_code(void) {
    static const OAuth2ProviderOps bad_ops = {
        .name = "bad",
        .build_auth_url = mock_build_auth_url,
        .exchange_code = NULL,
        .refresh_token = mock_refresh_token,
    };
    TEST_ASSERT_EQUAL_INT(-1, oauth2_store_register_provider(g_store, &bad_ops));
}

/* ========================================================================= */
/* Pending auth overflow                                                     */
/* ========================================================================= */

void test_pending_auth_overflow(void) {
    oauth2_store_register_provider(g_store, &mock_ops);

    /* Fill up all 16 pending auth slots */
    for (int i = 0; i < 16; i++) {
        OAuth2AuthRequest req = {0};
        OAuth2Error err = oauth2_store_begin_auth(g_store, "mock", "client123",
                                                   "email", &req);
        TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    }

    /* The 17th should fail */
    OAuth2AuthRequest req = {0};
    OAuth2Error err = oauth2_store_begin_auth(g_store, "mock", "client123",
                                               "email", &req);
    TEST_ASSERT_EQUAL_INT(OAUTH2_ERROR_STORAGE, err);
}

/* ========================================================================= */
/* Multiple providers                                                        */
/* ========================================================================= */

static char *mock2_build_auth_url(const char *client_id, const char *redirect_uri,
                                  const char *scope, const char *state,
                                  const char *code_challenge) {
    char *url = malloc(512);
    if (!url) return NULL;
    snprintf(url, 512, "https://mock2.example.com/auth?client_id=%s&state=%s"
             "&code_challenge=%s",
             client_id, state, code_challenge);
    (void)redirect_uri; (void)scope;
    return url;
}

static OAuth2Error mock2_exchange_code(const char *client_id, const char *client_secret,
                                       const char *redirect_uri, const char *code,
                                       const char *code_verifier,
                                       char *access_token, size_t at_len,
                                       char *refresh_token, size_t rt_len,
                                       int64_t *expires_in) {
    (void)client_id; (void)client_secret; (void)redirect_uri;
    (void)code; (void)code_verifier;
    snprintf(access_token, at_len, "mock2_access_token");
    snprintf(refresh_token, rt_len, "mock2_refresh_token");
    *expires_in = 7200;
    return OAUTH2_OK;
}

static const OAuth2ProviderOps mock2_ops = {
    .name = "mock2",
    .build_auth_url = mock2_build_auth_url,
    .exchange_code = mock2_exchange_code,
    .refresh_token = NULL,
};

void test_multiple_providers(void) {
    oauth2_store_register_provider(g_store, &mock_ops);
    TEST_ASSERT_EQUAL_INT(0, oauth2_store_register_provider(g_store, &mock2_ops));

    /* Auth flow with provider 1 */
    OAuth2AuthRequest req1 = {0};
    oauth2_store_begin_auth(g_store, "mock", "client1", "email", &req1);
    oauth2_store_complete_auth(g_store, req1.state, "code1",
                                "client1", "secret1", "user1@test.com");

    /* Auth flow with provider 2 */
    OAuth2AuthRequest req2 = {0};
    oauth2_store_begin_auth(g_store, "mock2", "client2", "email", &req2);
    oauth2_store_complete_auth(g_store, req2.state, "code2",
                                "client2", "secret2", "user2@test.com");

    /* Verify both tokens exist independently */
    TEST_ASSERT_TRUE(oauth2_store_has_token(g_store, "mock", "user1@test.com"));
    TEST_ASSERT_TRUE(oauth2_store_has_token(g_store, "mock2", "user2@test.com"));
    TEST_ASSERT_FALSE(oauth2_store_has_token(g_store, "mock", "user2@test.com"));
    TEST_ASSERT_FALSE(oauth2_store_has_token(g_store, "mock2", "user1@test.com"));

    /* Retrieve tokens and verify they're from the correct provider */
    OAuth2TokenResult r1 = {0};
    OAuth2Error err = oauth2_store_get_access_token(g_store, "mock",
                                                     "user1@test.com", "client1",
                                                     "secret1", &r1);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("mock_access_token", r1.access_token);

    OAuth2TokenResult r2 = {0};
    err = oauth2_store_get_access_token(g_store, "mock2",
                                         "user2@test.com", "client2",
                                         "secret2", &r2);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("mock2_access_token", r2.access_token);

    /* Revoking one doesn't affect the other */
    oauth2_store_revoke_token(g_store, "mock", "user1@test.com");
    TEST_ASSERT_FALSE(oauth2_store_has_token(g_store, "mock", "user1@test.com"));
    TEST_ASSERT_TRUE(oauth2_store_has_token(g_store, "mock2", "user2@test.com"));
}

/* ========================================================================= */
/* Encryption with auto-refresh                                              */
/* ========================================================================= */

void test_encrypted_token_auto_refresh(void) {
    char enc_db[256];
    snprintf(enc_db, sizeof(enc_db), "/tmp/test_oauth2_enc_refresh_%d.db", getpid());
    unlink_sqlite_db(enc_db);

    OAuth2Config cfg = {
        .db_path = enc_db,
        .redirect_uri = "http://localhost:8080/api/v1/oauth2/callback",
        .encryption_key = (const unsigned char *)TEST_ENCRYPTION_KEY,
        .encryption_key_len = strlen(TEST_ENCRYPTION_KEY),
    };
    oauth2_store_t *enc_store = oauth2_store_create(&cfg);
    TEST_ASSERT_NOT_NULL(enc_store);
    oauth2_store_register_provider(enc_store, &mock_ops);

    /* Store a token that's already expired (expires_in = 1 second) */
    mock_expires_in = 1;
    OAuth2AuthRequest req = {0};
    oauth2_store_begin_auth(enc_store, "mock", "client123", "email", &req);
    oauth2_store_complete_auth(enc_store, req.state, "code",
                                "client123", "secret", "user@test.com");
    mock_expires_in = 3600;

    /* Retrieve — should auto-refresh and the refreshed token should be encrypted */
    OAuth2TokenResult result = {0};
    OAuth2Error err = oauth2_store_get_access_token(enc_store, "mock",
                                                     "user@test.com", "client123",
                                                     "secret", &result);
    TEST_ASSERT_EQUAL_INT(OAUTH2_OK, err);
    TEST_ASSERT_EQUAL_STRING("refreshed_access_token", result.access_token);
    TEST_ASSERT_EQUAL_INT(1, mock_refresh_called);

    /* Verify the refreshed token is encrypted in the DB (not plaintext) */
    sqlite3 *db = NULL;
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(enc_db, &db));

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db,
        "SELECT access_token FROM oauth2_tokens "
        "WHERE provider='mock' AND account_id='user@test.com'",
        -1, &stmt, NULL);
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));

    const char *raw_at = (const char *)sqlite3_column_text(stmt, 0);
    TEST_ASSERT_NOT_NULL(raw_at);
    TEST_ASSERT_FALSE(strcmp(raw_at, "refreshed_access_token") == 0);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    oauth2_store_destroy(enc_store);
    unlink_sqlite_db(enc_db);
}

/* ========================================================================= */
/* Runner                                                                    */
/* ========================================================================= */

int main(void) {
    UNITY_BEGIN();

    /* Lifecycle */
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_create_null_config);
    RUN_TEST(test_create_null_db_path);

    /* Provider registry */
    RUN_TEST(test_register_provider);
    RUN_TEST(test_register_duplicate_provider);
    RUN_TEST(test_register_null_safety);
    RUN_TEST(test_register_provider_null_build_auth_url);
    RUN_TEST(test_register_provider_null_exchange_code);

    /* Begin auth */
    RUN_TEST(test_begin_auth);
    RUN_TEST(test_begin_auth_unknown_provider);
    RUN_TEST(test_begin_auth_null_safety);
    RUN_TEST(test_pending_auth_overflow);

    /* Complete auth */
    RUN_TEST(test_complete_auth_flow);
    RUN_TEST(test_complete_auth_invalid_state);
    RUN_TEST(test_complete_auth_exchange_failure);

    /* Token access */
    RUN_TEST(test_get_access_token);
    RUN_TEST(test_get_access_token_not_found);
    RUN_TEST(test_get_access_token_auto_refresh);
    RUN_TEST(test_get_access_token_refresh_failure);

    /* Revoke */
    RUN_TEST(test_revoke_token);

    /* Multiple providers */
    RUN_TEST(test_multiple_providers);

    /* Expire pending */
    RUN_TEST(test_cleanup);
    RUN_TEST(test_has_token_false_when_empty);

    /* Encryption */
    RUN_TEST(test_encrypted_token_roundtrip);
    RUN_TEST(test_encrypted_token_not_plaintext_in_db);
    RUN_TEST(test_plaintext_in_db_fails_with_encryption);
    RUN_TEST(test_encrypted_token_auto_refresh);

    /* NULL safety */
    RUN_TEST(test_null_safety);

    return UNITY_END();
}
