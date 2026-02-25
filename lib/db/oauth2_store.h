/*
 * oauth2_store.h - OAuth2 Token Store
 *
 * Generic OAuth2 framework with PKCE, AES-256-GCM token encryption,
 * SQLite-backed storage, and a vtable-based provider pattern.
 * Ported from sage's oauth2.h, adapted to scaffold's sqlite_dal abstraction.
 */

#ifndef OAUTH2_STORE_H
#define OAUTH2_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OAUTH2_MAX_TOKEN_LEN      2048
#define OAUTH2_MAX_ACCOUNT_ID_LEN 128

typedef struct oauth2_store oauth2_store_t;

typedef enum {
    OAUTH2_OK = 0,
    OAUTH2_ERROR_INVALID,
    OAUTH2_ERROR_NETWORK,
    OAUTH2_ERROR_PROVIDER,
    OAUTH2_ERROR_EXPIRED,
    OAUTH2_ERROR_NOT_FOUND,
    OAUTH2_ERROR_STORAGE,
} OAuth2Error;

typedef struct {
    const char *name;
    char *(*build_auth_url)(const char *client_id, const char *redirect_uri,
                            const char *scope, const char *state,
                            const char *code_challenge);
    OAuth2Error (*exchange_code)(const char *client_id, const char *client_secret,
                                const char *redirect_uri, const char *code,
                                const char *code_verifier,
                                char *access_token, size_t at_len,
                                char *refresh_token, size_t rt_len,
                                int64_t *expires_in);
    OAuth2Error (*refresh_token)(const char *client_id, const char *client_secret,
                                 const char *refresh_token_in,
                                 char *access_token, size_t at_len,
                                 char *new_refresh_token, size_t rt_len,
                                 int64_t *expires_in);
    OAuth2Error (*revoke_token)(const char *client_id, const char *access_token);
} OAuth2ProviderOps;

typedef struct {
    const char *db_path;
    const char *redirect_uri;
    const unsigned char *encryption_key;
    size_t encryption_key_len;
} OAuth2Config;

typedef struct {
    char auth_url[2048];
    char state[64];
} OAuth2AuthRequest;

typedef struct {
    char access_token[OAUTH2_MAX_TOKEN_LEN];
    int64_t expires_at;
} OAuth2TokenResult;

/* Error string conversion */
const char *oauth2_error_string(OAuth2Error err);

/* Lifecycle */
oauth2_store_t *oauth2_store_create(const OAuth2Config *config);
void oauth2_store_destroy(oauth2_store_t *o);

/* Provider registry */
int oauth2_store_register_provider(oauth2_store_t *o, const OAuth2ProviderOps *ops);

/* Authorization code flow with PKCE */
OAuth2Error oauth2_store_begin_auth(oauth2_store_t *o, const char *provider,
                                     const char *client_id, const char *scope,
                                     OAuth2AuthRequest *out);
OAuth2Error oauth2_store_complete_auth(oauth2_store_t *o, const char *state,
                                        const char *code, const char *client_id,
                                        const char *client_secret,
                                        const char *account_id);

/* Token access (auto-refreshes with 60s margin) */
OAuth2Error oauth2_store_get_access_token(oauth2_store_t *o, const char *provider,
                                           const char *account_id, const char *client_id,
                                           const char *client_secret,
                                           OAuth2TokenResult *result);

bool oauth2_store_has_token(oauth2_store_t *o, const char *provider,
                             const char *account_id);
OAuth2Error oauth2_store_revoke_token(oauth2_store_t *o, const char *provider,
                                       const char *account_id);
void oauth2_store_expire_pending(oauth2_store_t *o);

#endif /* OAUTH2_STORE_H */
