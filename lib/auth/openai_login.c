#include "openai_login.h"
#include "openai_oauth_provider.h"
#include "oauth_callback_server.h"
#include "jwt_decode.h"
#include "../util/app_home.h"
#include "../util/process_spawn.h"
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CALLBACK_PORT    1455
#define CALLBACK_TIMEOUT 300
#define ENCRYPTION_SALT  "scaffold-oauth2-v1"
#define ENCRYPTION_KEY_LEN 32
#define ACCOUNT_ID_KEY   "https://api.openai.com/auth"
#define ACCOUNT_ID_FIELD "chatgpt_account_id"

/* Derive a per-user encryption key from UID + hostname + salt.
 * Not a substitute for a system keychain, but ensures the key
 * differs across users and machines. */
static void derive_encryption_key(unsigned char out[ENCRYPTION_KEY_LEN]) {
    char material[512];
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    snprintf(material, sizeof(material), "%s:%u:%s", ENCRYPTION_SALT, (unsigned)getuid(), hostname);
    mbedtls_sha256((const unsigned char *)material, strlen(material), out, 0);
    mbedtls_platform_zeroize(material, sizeof(material));
}

/* Wildcard account_id for providers like OpenAI where account is
 * embedded in the JWT rather than known before auth completes. */
#define DEFAULT_ACCOUNT  "default"

static int is_headless_env(void) {
    if (getenv("SSH_CLIENT") || getenv("SSH_TTY")) return 1;
    if (getenv("CODESPACES") || getenv("REMOTE_CONTAINERS")) return 1;
#ifdef __linux__
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) return 1;
#endif
    return 0;
}

static void open_browser(const char *url) {
    pid_t pid;
#ifdef __APPLE__
    char *args[] = { "open", (char *)url, NULL };
#else
    char *args[] = { "xdg-open", (char *)url, NULL };
#endif
    process_spawn_devnull(args, &pid);
}

static oauth2_store_t *create_store(const char *db_path) {
    unsigned char key[ENCRYPTION_KEY_LEN];
    derive_encryption_key(key);

    OAuth2Config cfg = {
        .db_path = db_path,
        .redirect_uri = OPENAI_REDIRECT_URI,
        .encryption_key = key,
        .encryption_key_len = ENCRYPTION_KEY_LEN,
    };
    oauth2_store_t *store = oauth2_store_create(&cfg);
    mbedtls_platform_zeroize(key, sizeof(key));
    if (!store) return NULL;

    /* Restrict DB file permissions to owner only */
    chmod(db_path, 0600);

    if (oauth2_store_register_provider(store, openai_oauth_provider_ops()) != 0) {
        oauth2_store_destroy(store);
        return NULL;
    }
    return store;
}

int openai_login(const char *db_path, int headless) {
    if (!db_path) return -1;

    oauth2_store_t *store = create_store(db_path);
    if (!store) {
        fprintf(stderr, "Error: Failed to initialize OAuth2 store\n");
        return -1;
    }

    /* Begin PKCE auth flow */
    OAuth2AuthRequest auth = {0};
    OAuth2Error err = oauth2_store_begin_auth(store, OPENAI_PROVIDER_NAME,
                                               OPENAI_CLIENT_ID, OPENAI_SCOPE, &auth);
    if (err != OAUTH2_OK) {
        fprintf(stderr, "Error: Failed to begin OAuth2 authorization\n");
        oauth2_store_destroy(store);
        return -1;
    }

    int effective_headless = headless || is_headless_env();

    if (!effective_headless) {
        open_browser(auth.auth_url);
        printf("Opening browser for authentication...\n");
    }

    printf("\nIf the browser doesn't open, visit this URL:\n\n  %s\n\n", auth.auth_url);
    printf("Waiting for authentication...\n");

    /* Wait for OAuth callback */
    OAuthCallbackResult callback = {0};
    int rc = oauth_callback_server_wait(CALLBACK_PORT, CALLBACK_TIMEOUT, &callback);

    if (rc != 0 || !callback.success) {
        if (callback.error[0]) {
            fprintf(stderr, "Authentication error: %s\n", callback.error);
        } else {
            fprintf(stderr, "Authentication timed out or failed.\n");
        }
        oauth2_store_destroy(store);
        return -1;
    }

    /* Verify the round-tripped state matches what we sent (CSRF protection) */
    if (strcmp(auth.state, callback.state) != 0) {
        fprintf(stderr, "Error: OAuth state mismatch (possible CSRF attack)\n");
        oauth2_store_destroy(store);
        return -1;
    }

    /* Complete auth: exchange code for tokens */
    err = oauth2_store_complete_auth(store, callback.state, callback.code,
                                      OPENAI_CLIENT_ID, "", DEFAULT_ACCOUNT);
    if (err != OAUTH2_OK) {
        fprintf(stderr, "Error: Failed to exchange authorization code\n");
        oauth2_store_destroy(store);
        return -1;
    }

    /* Verify we can get credentials */
    OAuth2TokenResult token = {0};
    err = oauth2_store_get_access_token(store, OPENAI_PROVIDER_NAME,
                                         DEFAULT_ACCOUNT, OPENAI_CLIENT_ID,
                                         "", &token);

    if (err == OAUTH2_OK) {
        char acct_id[128] = {0};
        if (jwt_extract_nested_claim(token.access_token, ACCOUNT_ID_KEY,
                                      ACCOUNT_ID_FIELD, acct_id, sizeof(acct_id)) == 0) {
            printf("Logged in successfully (account: %s)\n", acct_id);
        } else {
            printf("Logged in successfully.\n");
        }
        mbedtls_platform_zeroize(token.access_token, sizeof(token.access_token));
    } else {
        printf("Login completed but token retrieval failed.\n");
    }

    oauth2_store_destroy(store);
    return 0;
}

int openai_is_logged_in(const char *db_path) {
    if (!db_path) return 0;

    oauth2_store_t *store = create_store(db_path);
    if (!store) return 0;

    int result = oauth2_store_has_token(store, OPENAI_PROVIDER_NAME, DEFAULT_ACCOUNT);
    oauth2_store_destroy(store);
    return result;
}

int openai_logout(const char *db_path) {
    if (!db_path) return -1;

    oauth2_store_t *store = create_store(db_path);
    if (!store) return -1;

    OAuth2Error err = oauth2_store_revoke_token(store, OPENAI_PROVIDER_NAME, DEFAULT_ACCOUNT);
    oauth2_store_destroy(store);

    if (err == OAUTH2_OK) {
        printf("Logged out of OpenAI.\n");
        return 0;
    }
    return -1;
}

int openai_get_codex_credentials(const char *db_path,
                                  char *access_token, size_t at_len,
                                  char *account_id, size_t aid_len) {
    if (!db_path || !access_token || !at_len || !account_id || !aid_len)
        return -1;

    oauth2_store_t *store = create_store(db_path);
    if (!store) return -1;

    OAuth2TokenResult token = {0};
    OAuth2Error err = oauth2_store_get_access_token(store, OPENAI_PROVIDER_NAME,
                                                     DEFAULT_ACCOUNT, OPENAI_CLIENT_ID,
                                                     "", &token);
    oauth2_store_destroy(store);

    if (err != OAUTH2_OK) return -1;

    /* Extract account ID from JWT */
    if (jwt_extract_nested_claim(token.access_token, ACCOUNT_ID_KEY,
                                  ACCOUNT_ID_FIELD, account_id, aid_len) != 0) {
        mbedtls_platform_zeroize(token.access_token, sizeof(token.access_token));
        return -1;
    }

    size_t tok_len = strlen(token.access_token);
    if (tok_len + 1 > at_len) {
        mbedtls_platform_zeroize(token.access_token, sizeof(token.access_token));
        return -1;
    }
    memcpy(access_token, token.access_token, tok_len + 1);
    mbedtls_platform_zeroize(token.access_token, sizeof(token.access_token));
    return 0;
}
