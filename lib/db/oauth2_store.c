/*
 * oauth2_store.c - OAuth2 Token Store Implementation
 *
 * SQLite-backed OAuth2 token storage with PKCE, AES-256-GCM encryption,
 * and HKDF key derivation via mbedTLS. Uses scaffold's sqlite_dal abstraction.
 */

#include "oauth2_store.h"
#include "sqlite_dal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

#define MAX_PROVIDERS          8
#define MAX_PENDING_AUTHS      16
#define PENDING_AUTH_TTL_S     300
#define TOKEN_REFRESH_MARGIN_S 60
#define VERIFIER_BYTES         32
#define STATE_BYTES            16
#define AES_KEY_LEN            32
#define GCM_IV_LEN             12
#define GCM_TAG_LEN            16
#define HKDF_CONTEXT           "oauth2-token-encryption-v1"
/* Encrypted tokens are base64(IV + ciphertext + tag), roughly 4/3 * (plain + 28).
 * A 2048-byte plaintext token produces ~2770 bytes of encrypted base64. */
#define ENCRYPTED_TOKEN_MAX    4096

typedef struct {
    char state[64];
    char code_verifier[64];
    char provider[64];
    char redirect_uri[512];
    int64_t created_at;
} PendingAuth;

struct oauth2_store {
    sqlite_dal_t *dal;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    char *redirect_uri;
    OAuth2ProviderOps providers[MAX_PROVIDERS];
    int provider_count;
    PendingAuth pending[MAX_PENDING_AUTHS];
    int pending_count;
    unsigned char derived_key[AES_KEY_LEN];
    bool encryption_enabled;
};

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS oauth2_tokens ("
    "  provider TEXT NOT NULL,"
    "  account_id TEXT NOT NULL,"
    "  access_token TEXT NOT NULL,"
    "  refresh_token TEXT NOT NULL,"
    "  expires_at INTEGER NOT NULL,"
    "  created_at INTEGER NOT NULL,"
    "  updated_at INTEGER NOT NULL,"
    "  PRIMARY KEY (provider, account_id)"
    ");";

/* ========================================================================= */
/* Error string conversion                                                   */
/* ========================================================================= */

const char *oauth2_error_string(OAuth2Error err) {
    switch (err) {
    case OAUTH2_OK:              return "success";
    case OAUTH2_ERROR_INVALID:   return "invalid parameters";
    case OAUTH2_ERROR_NETWORK:   return "network error";
    case OAUTH2_ERROR_PROVIDER:  return "provider error";
    case OAUTH2_ERROR_EXPIRED:   return "token expired and refresh failed";
    case OAUTH2_ERROR_NOT_FOUND: return "token not found";
    case OAUTH2_ERROR_STORAGE:   return "storage or encryption error";
    }
    return "unknown error";
}

/* ========================================================================= */
/* Crypto helpers (PKCE + AES-256-GCM)                                       */
/* ========================================================================= */

static int base64url_encode(const unsigned char *in, size_t in_len,
                            char *out, size_t out_len) {
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, out_len, &olen, in, in_len) != 0)
        return -1;
    for (size_t i = 0; i < olen; i++) {
        if (out[i] == '+') out[i] = '-';
        else if (out[i] == '/') out[i] = '_';
    }
    while (olen > 0 && out[olen - 1] == '=') olen--;
    out[olen] = '\0';
    return 0;
}

static int generate_random_base64url(mbedtls_ctr_drbg_context *ctr_drbg,
                                     size_t num_bytes, char *out, size_t out_len) {
    unsigned char buf[64];
    if (num_bytes > sizeof(buf)) return -1;
    if (mbedtls_ctr_drbg_random(ctr_drbg, buf, num_bytes) != 0)
        return -1;
    return base64url_encode(buf, num_bytes, out, out_len);
}

static int generate_pkce(mbedtls_ctr_drbg_context *ctr_drbg,
                         char *verifier, size_t v_len,
                         char *challenge, size_t c_len) {
    if (generate_random_base64url(ctr_drbg, VERIFIER_BYTES, verifier, v_len) != 0)
        return -1;
    unsigned char sha[32];
    if (mbedtls_sha256((const unsigned char *)verifier, strlen(verifier), sha, 0) != 0)
        return -1;
    return base64url_encode(sha, 32, challenge, c_len);
}

static char *encrypt_token(oauth2_store_t *o, const char *plaintext) {
    if (!o->encryption_enabled || !plaintext) return NULL;

    size_t pt_len = strlen(plaintext);
    size_t ct_len = GCM_IV_LEN + pt_len + GCM_TAG_LEN;
    unsigned char *buf = malloc(ct_len);
    if (!buf) return NULL;

    unsigned char *iv = buf;
    unsigned char *ciphertext = buf + GCM_IV_LEN;
    unsigned char *tag = buf + GCM_IV_LEN + pt_len;

    if (mbedtls_ctr_drbg_random(&o->ctr_drbg, iv, GCM_IV_LEN) != 0) {
        free(buf);
        return NULL;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  o->derived_key, AES_KEY_LEN * 8);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        free(buf);
        return NULL;
    }

    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                     pt_len, iv, GCM_IV_LEN,
                                     NULL, 0,
                                     (const unsigned char *)plaintext,
                                     ciphertext, GCM_TAG_LEN, tag);
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        free(buf);
        return NULL;
    }

    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, buf, ct_len);

    char *result = malloc(b64_len + 1);
    if (!result) {
        free(buf);
        return NULL;
    }

    mbedtls_base64_encode((unsigned char *)result, b64_len + 1, &b64_len, buf, ct_len);
    result[b64_len] = '\0';
    free(buf);
    return result;
}

static int decrypt_token(oauth2_store_t *o, const char *b64_input,
                         char *out, size_t out_len) {
    if (!o->encryption_enabled || !b64_input) return -1;

    size_t raw_len = 0;
    if (mbedtls_base64_decode(NULL, 0, &raw_len,
                               (const unsigned char *)b64_input, strlen(b64_input))
            != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return -1;
    }

    if (raw_len < GCM_IV_LEN + GCM_TAG_LEN) return -1;

    unsigned char *raw = malloc(raw_len);
    if (!raw) return -1;

    size_t decoded = 0;
    if (mbedtls_base64_decode(raw, raw_len, &decoded,
                               (const unsigned char *)b64_input, strlen(b64_input)) != 0) {
        free(raw);
        return -1;
    }

    size_t pt_len = decoded - GCM_IV_LEN - GCM_TAG_LEN;
    if (pt_len + 1 > out_len) {
        free(raw);
        return -1;
    }

    const unsigned char *iv = raw;
    const unsigned char *ciphertext = raw + GCM_IV_LEN;
    const unsigned char *tag = raw + GCM_IV_LEN + pt_len;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  o->derived_key, AES_KEY_LEN * 8);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        free(raw);
        return -1;
    }

    ret = mbedtls_gcm_auth_decrypt(&gcm, pt_len, iv, GCM_IV_LEN,
                                    NULL, 0, tag, GCM_TAG_LEN,
                                    ciphertext, (unsigned char *)out);
    mbedtls_gcm_free(&gcm);
    free(raw);

    if (ret != 0) return -1;

    out[pt_len] = '\0';
    return 0;
}

/* ========================================================================= */
/* In-memory pending auth helpers                                            */
/* ========================================================================= */

static const OAuth2ProviderOps *find_provider(const oauth2_store_t *o,
                                               const char *name) {
    for (int i = 0; i < o->provider_count; i++) {
        if (strcmp(o->providers[i].name, name) == 0)
            return &o->providers[i];
    }
    return NULL;
}

static void expire_pending_auths(oauth2_store_t *o) {
    int64_t now = (int64_t)time(NULL);
    int write_idx = 0;
    for (int i = 0; i < o->pending_count; i++) {
        if (now - o->pending[i].created_at < PENDING_AUTH_TTL_S) {
            if (write_idx != i) o->pending[write_idx] = o->pending[i];
            write_idx++;
        }
    }
    o->pending_count = write_idx;
}

static PendingAuth *find_pending_by_state(oauth2_store_t *o, const char *state) {
    for (int i = 0; i < o->pending_count; i++) {
        if (strcmp(o->pending[i].state, state) == 0)
            return &o->pending[i];
    }
    return NULL;
}

static void remove_pending(oauth2_store_t *o, int index) {
    if (index < 0 || index >= o->pending_count) return;
    for (int i = index; i < o->pending_count - 1; i++)
        o->pending[i] = o->pending[i + 1];
    o->pending_count--;
}

/* ========================================================================= */
/* Custom binder types for DAL parameterized queries                         */
/* ========================================================================= */

typedef struct {
    const char *provider;
    const char *account_id;
    const char *access_token;
    const char *refresh_token;
    int64_t expires_at;
    int64_t created_at;
    int64_t updated_at;
} BindOAuth2Upsert;

static int bind_oauth2_upsert(sqlite3_stmt *stmt, void *data) {
    BindOAuth2Upsert *b = data;
    sqlite3_bind_text(stmt, 1, b->provider, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, b->account_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, b->access_token, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, b->refresh_token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, b->expires_at);
    sqlite3_bind_int64(stmt, 6, b->created_at);
    sqlite3_bind_int64(stmt, 7, b->updated_at);
    return 0;
}

typedef struct {
    const char *access_token;
    int64_t expires_at;
    int64_t updated_at;
    const char *provider;
    const char *account_id;
} BindOAuth2Refresh;

static int bind_oauth2_refresh(sqlite3_stmt *stmt, void *data) {
    BindOAuth2Refresh *b = data;
    sqlite3_bind_text(stmt, 1, b->access_token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, b->expires_at);
    sqlite3_bind_int64(stmt, 3, b->updated_at);
    sqlite3_bind_text(stmt, 4, b->provider, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, b->account_id, -1, SQLITE_STATIC);
    return 0;
}

typedef struct {
    const char *access_token;
    const char *refresh_token;
    int64_t expires_at;
    int64_t updated_at;
    const char *provider;
    const char *account_id;
} BindOAuth2RefreshRotate;

static int bind_oauth2_refresh_rotate(sqlite3_stmt *stmt, void *data) {
    BindOAuth2RefreshRotate *b = data;
    sqlite3_bind_text(stmt, 1, b->access_token, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, b->refresh_token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, b->expires_at);
    sqlite3_bind_int64(stmt, 4, b->updated_at);
    sqlite3_bind_text(stmt, 5, b->provider, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, b->account_id, -1, SQLITE_STATIC);
    return 0;
}

/* Row type for token SELECT queries (buffers sized for encrypted base64) */
typedef struct {
    char access_token[ENCRYPTED_TOKEN_MAX];
    char refresh_token[ENCRYPTED_TOKEN_MAX];
    int64_t expires_at;
} TokenRow;

static void *map_token_row(sqlite3_stmt *stmt, void *user_data) {
    (void)user_data;
    TokenRow *row = calloc(1, sizeof(TokenRow));
    if (!row) return NULL;
    const char *at = (const char *)sqlite3_column_text(stmt, 0);
    const char *rt = (const char *)sqlite3_column_text(stmt, 1);
    if (at) snprintf(row->access_token, sizeof(row->access_token), "%s", at);
    if (rt) snprintf(row->refresh_token, sizeof(row->refresh_token), "%s", rt);
    row->expires_at = sqlite3_column_int64(stmt, 2);
    return row;
}

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

oauth2_store_t *oauth2_store_create(const OAuth2Config *config) {
    if (!config || !config->db_path) return NULL;

    oauth2_store_t *o = calloc(1, sizeof(oauth2_store_t));
    if (!o) return NULL;

    mbedtls_entropy_init(&o->entropy);
    mbedtls_ctr_drbg_init(&o->ctr_drbg);

    const char *pers = "oauth2";
    if (mbedtls_ctr_drbg_seed(&o->ctr_drbg, mbedtls_entropy_func,
                               &o->entropy,
                               (const unsigned char *)pers, strlen(pers)) != 0) {
        goto fail_csprng;
    }

    sqlite_dal_config_t dal_config = SQLITE_DAL_CONFIG_DEFAULT;
    dal_config.db_path = config->db_path;
    dal_config.default_name = "oauth2.db";
    dal_config.schema_sql = SCHEMA_SQL;

    o->dal = sqlite_dal_create(&dal_config);
    if (!o->dal) goto fail_csprng;

    if (config->redirect_uri)
        o->redirect_uri = strdup(config->redirect_uri);

    if (config->encryption_key && config->encryption_key_len > 0) {
        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (mbedtls_hkdf(md, NULL, 0,
                          config->encryption_key, config->encryption_key_len,
                          (const unsigned char *)HKDF_CONTEXT, strlen(HKDF_CONTEXT),
                          o->derived_key, AES_KEY_LEN) != 0) {
            goto fail_dal;
        }
        o->encryption_enabled = true;
    }

    return o;

fail_dal:
    free(o->redirect_uri);
    sqlite_dal_destroy(o->dal);
fail_csprng:
    mbedtls_ctr_drbg_free(&o->ctr_drbg);
    mbedtls_entropy_free(&o->entropy);
    free(o);
    return NULL;
}

void oauth2_store_destroy(oauth2_store_t *o) {
    if (!o) return;
    mbedtls_platform_zeroize(o->derived_key, sizeof(o->derived_key));
    free(o->redirect_uri);
    sqlite_dal_destroy(o->dal);
    mbedtls_ctr_drbg_free(&o->ctr_drbg);
    mbedtls_entropy_free(&o->entropy);
    free(o);
}

/* ========================================================================= */
/* Provider registry                                                         */
/* ========================================================================= */

int oauth2_store_register_provider(oauth2_store_t *o, const OAuth2ProviderOps *ops) {
    if (!o || !ops || !ops->name || !ops->build_auth_url || !ops->exchange_code)
        return -1;
    if (o->provider_count >= MAX_PROVIDERS) return -1;
    if (find_provider(o, ops->name)) return -1;
    o->providers[o->provider_count++] = *ops;
    return 0;
}

/* ========================================================================= */
/* Authorization code flow                                                   */
/* ========================================================================= */

OAuth2Error oauth2_store_begin_auth(oauth2_store_t *o, const char *provider,
                                     const char *client_id, const char *scope,
                                     OAuth2AuthRequest *out) {
    if (!o || !provider || !client_id || !out)
        return OAUTH2_ERROR_INVALID;

    const OAuth2ProviderOps *ops = find_provider(o, provider);
    if (!ops) return OAUTH2_ERROR_PROVIDER;

    expire_pending_auths(o);
    if (o->pending_count >= MAX_PENDING_AUTHS)
        return OAUTH2_ERROR_STORAGE;

    char state[64];
    if (generate_random_base64url(&o->ctr_drbg, STATE_BYTES,
                                   state, sizeof(state)) != 0)
        return OAUTH2_ERROR_INVALID;

    char verifier[64];
    char challenge[64];
    if (generate_pkce(&o->ctr_drbg, verifier, sizeof(verifier),
                      challenge, sizeof(challenge)) != 0)
        return OAUTH2_ERROR_INVALID;

    const char *redirect = o->redirect_uri ? o->redirect_uri : "";
    char *url = ops->build_auth_url(client_id, redirect, scope, state, challenge);
    if (!url) return OAUTH2_ERROR_PROVIDER;

    size_t url_len = strlen(url);
    if (url_len >= sizeof(out->auth_url)) {
        free(url);
        return OAUTH2_ERROR_INVALID;
    }

    memcpy(out->auth_url, url, url_len + 1);
    free(url);

    size_t state_len = strlen(state);
    if (state_len >= sizeof(out->state)) return OAUTH2_ERROR_INVALID;
    memcpy(out->state, state, state_len + 1);

    PendingAuth *pa = &o->pending[o->pending_count++];
    memset(pa, 0, sizeof(*pa));
    snprintf(pa->state, sizeof(pa->state), "%s", state);
    snprintf(pa->code_verifier, sizeof(pa->code_verifier), "%s", verifier);
    snprintf(pa->provider, sizeof(pa->provider), "%s", provider);
    if (o->redirect_uri)
        snprintf(pa->redirect_uri, sizeof(pa->redirect_uri), "%s", o->redirect_uri);
    pa->created_at = (int64_t)time(NULL);

    return OAUTH2_OK;
}

OAuth2Error oauth2_store_complete_auth(oauth2_store_t *o, const char *state,
                                        const char *code, const char *client_id,
                                        const char *client_secret,
                                        const char *account_id) {
    if (!o || !state || !code || !client_id || !account_id)
        return OAUTH2_ERROR_INVALID;

    expire_pending_auths(o);

    PendingAuth *pa = find_pending_by_state(o, state);
    if (!pa) return OAUTH2_ERROR_NOT_FOUND;

    const OAuth2ProviderOps *ops = find_provider(o, pa->provider);
    if (!ops) return OAUTH2_ERROR_PROVIDER;

    char access_token[OAUTH2_MAX_TOKEN_LEN] = {0};
    char refresh_token[OAUTH2_MAX_TOKEN_LEN] = {0};
    int64_t expires_in = 0;
    OAuth2Error result = OAUTH2_OK;

    OAuth2Error err = ops->exchange_code(
        client_id, client_secret ? client_secret : "",
        pa->redirect_uri, code, pa->code_verifier,
        access_token, sizeof(access_token),
        refresh_token, sizeof(refresh_token),
        &expires_in);

    int pa_idx = (int)(pa - o->pending);
    remove_pending(o, pa_idx);

    if (err != OAUTH2_OK) { result = err; goto zeroize; }

    int64_t now = (int64_t)time(NULL);
    int64_t expires_at = now + expires_in;

    char *enc_at = NULL;
    char *enc_rt = NULL;
    if (o->encryption_enabled) {
        enc_at = encrypt_token(o, access_token);
        enc_rt = encrypt_token(o, refresh_token);
        if (!enc_at || !enc_rt) {
            free(enc_at);
            free(enc_rt);
            result = OAUTH2_ERROR_STORAGE;
            goto zeroize;
        }
    }

    BindOAuth2Upsert params = {
        .provider     = ops->name,
        .account_id   = account_id,
        .access_token = enc_at ? enc_at : access_token,
        .refresh_token = enc_rt ? enc_rt : refresh_token,
        .expires_at   = expires_at,
        .created_at   = now,
        .updated_at   = now,
    };

    int rc = sqlite_dal_exec_p(o->dal,
        "INSERT INTO oauth2_tokens (provider, account_id, access_token, "
        "refresh_token, expires_at, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(provider, account_id) DO UPDATE SET "
        "access_token=excluded.access_token, refresh_token=excluded.refresh_token, "
        "expires_at=excluded.expires_at, updated_at=excluded.updated_at",
        bind_oauth2_upsert, &params);

    free(enc_at);
    free(enc_rt);

    result = (rc >= 0) ? OAUTH2_OK : OAUTH2_ERROR_STORAGE;

zeroize:
    mbedtls_platform_zeroize(access_token, sizeof(access_token));
    mbedtls_platform_zeroize(refresh_token, sizeof(refresh_token));
    return result;
}

/* ========================================================================= */
/* Token access (auto-refresh)                                               */
/* ========================================================================= */

/* Heap-allocated context for token refresh to reduce stack usage.
 * Each field is OAUTH2_MAX_TOKEN_LEN (2048) bytes. */
typedef struct {
    char plain_at[OAUTH2_MAX_TOKEN_LEN];
    char plain_rt[OAUTH2_MAX_TOKEN_LEN];
    char new_at[OAUTH2_MAX_TOKEN_LEN];
    char new_rt[OAUTH2_MAX_TOKEN_LEN];
} TokenRefreshCtx;

static OAuth2Error load_and_decrypt_tokens(oauth2_store_t *o,
                                            const TokenRow *row,
                                            TokenRefreshCtx *ctx) {
    if (o->encryption_enabled) {
        if (decrypt_token(o, row->access_token, ctx->plain_at,
                          sizeof(ctx->plain_at)) != 0)
            return OAUTH2_ERROR_STORAGE;
        if (row->refresh_token[0] != '\0') {
            if (decrypt_token(o, row->refresh_token, ctx->plain_rt,
                              sizeof(ctx->plain_rt)) != 0)
                return OAUTH2_ERROR_STORAGE;
        }
    } else {
        size_t at_len = strlen(row->access_token);
        size_t rt_len = strlen(row->refresh_token);
        if (at_len >= sizeof(ctx->plain_at)) return OAUTH2_ERROR_STORAGE;
        memcpy(ctx->plain_at, row->access_token, at_len + 1);
        if (rt_len < sizeof(ctx->plain_rt))
            memcpy(ctx->plain_rt, row->refresh_token, rt_len + 1);
    }
    return OAUTH2_OK;
}

static OAuth2Error do_refresh_and_store(oauth2_store_t *o,
                                         const char *provider,
                                         const char *account_id,
                                         const char *client_id,
                                         const char *client_secret,
                                         TokenRefreshCtx *ctx,
                                         OAuth2TokenResult *result) {
    const OAuth2ProviderOps *ops = find_provider(o, provider);
    if (!ops || !ops->refresh_token)
        return OAUTH2_ERROR_PROVIDER;

    int64_t new_expires_in = 0;
    OAuth2Error err = ops->refresh_token(
        client_id ? client_id : "", client_secret ? client_secret : "",
        ctx->plain_rt, ctx->new_at, sizeof(ctx->new_at),
        ctx->new_rt, sizeof(ctx->new_rt), &new_expires_in);
    if (err != OAUTH2_OK)
        return OAUTH2_ERROR_EXPIRED;

    int64_t new_expires_at = (int64_t)time(NULL) + new_expires_in;

    if (ctx->new_rt[0] != '\0') {
        char *enc_at = NULL, *enc_rt = NULL;
        if (o->encryption_enabled) {
            enc_at = encrypt_token(o, ctx->new_at);
            enc_rt = encrypt_token(o, ctx->new_rt);
            if (!enc_at || !enc_rt) {
                free(enc_at); free(enc_rt);
                return OAUTH2_ERROR_STORAGE;
            }
        }
        BindOAuth2RefreshRotate p = {
            .access_token  = enc_at ? enc_at : ctx->new_at,
            .refresh_token = enc_rt ? enc_rt : ctx->new_rt,
            .expires_at    = new_expires_at,
            .updated_at    = (int64_t)time(NULL),
            .provider      = provider,
            .account_id    = account_id,
        };
        int rc = sqlite_dal_exec_p(o->dal,
            "UPDATE oauth2_tokens SET access_token = ?, refresh_token = ?, "
            "expires_at = ?, updated_at = ? WHERE provider = ? AND account_id = ?",
            bind_oauth2_refresh_rotate, &p);
        free(enc_at); free(enc_rt);
        if (rc < 0) return OAUTH2_ERROR_STORAGE;
    } else {
        char *enc_at = NULL;
        if (o->encryption_enabled) {
            enc_at = encrypt_token(o, ctx->new_at);
            if (!enc_at) return OAUTH2_ERROR_STORAGE;
        }
        BindOAuth2Refresh p = {
            .access_token = enc_at ? enc_at : ctx->new_at,
            .expires_at   = new_expires_at,
            .updated_at   = (int64_t)time(NULL),
            .provider     = provider,
            .account_id   = account_id,
        };
        int rc = sqlite_dal_exec_p(o->dal,
            "UPDATE oauth2_tokens SET access_token = ?, expires_at = ?, "
            "updated_at = ? WHERE provider = ? AND account_id = ?",
            bind_oauth2_refresh, &p);
        free(enc_at);
        if (rc < 0) return OAUTH2_ERROR_STORAGE;
    }

    snprintf(result->access_token, sizeof(result->access_token), "%s", ctx->new_at);
    result->expires_at = new_expires_at;
    return OAUTH2_OK;
}

OAuth2Error oauth2_store_get_access_token(oauth2_store_t *o, const char *provider,
                                           const char *account_id,
                                           const char *client_id,
                                           const char *client_secret,
                                           OAuth2TokenResult *result) {
    if (!o || !provider || !account_id || !result)
        return OAUTH2_ERROR_INVALID;

    memset(result, 0, sizeof(*result));

    BindText2 select_params = { provider, account_id };
    TokenRow *row = sqlite_dal_query_one_p(o->dal,
        "SELECT access_token, refresh_token, expires_at FROM oauth2_tokens "
        "WHERE provider = ? AND account_id = ?",
        bind_text2, &select_params, map_token_row, NULL);

    if (!row) return OAUTH2_ERROR_NOT_FOUND;

    TokenRefreshCtx *ctx = calloc(1, sizeof(TokenRefreshCtx));
    if (!ctx) { free(row); return OAUTH2_ERROR_STORAGE; }

    OAuth2Error ret = load_and_decrypt_tokens(o, row, ctx);
    int64_t expires_at = row->expires_at;
    free(row);

    if (ret != OAUTH2_OK) goto cleanup;

    int64_t now = (int64_t)time(NULL);
    if (expires_at > now + TOKEN_REFRESH_MARGIN_S) {
        snprintf(result->access_token, sizeof(result->access_token), "%s", ctx->plain_at);
        result->expires_at = expires_at;
        goto cleanup;
    }

    if (ctx->plain_rt[0] == '\0') {
        ret = OAUTH2_ERROR_EXPIRED;
        goto cleanup;
    }

    ret = do_refresh_and_store(o, provider, account_id, client_id, client_secret,
                               ctx, result);

cleanup:
    mbedtls_platform_zeroize(ctx, sizeof(*ctx));
    free(ctx);
    return ret;
}

/* ========================================================================= */
/* Utilities                                                                 */
/* ========================================================================= */

bool oauth2_store_has_token(oauth2_store_t *o, const char *provider,
                             const char *account_id) {
    if (!o || !provider || !account_id) return false;

    BindText2 params = { provider, account_id };
    int rc = sqlite_dal_exists_p(o->dal,
        "SELECT 1 FROM oauth2_tokens "
        "WHERE provider = ? AND account_id = ? LIMIT 1",
        bind_text2, &params);
    return rc == 1;
}

OAuth2Error oauth2_store_revoke_token(oauth2_store_t *o, const char *provider,
                                       const char *account_id) {
    if (!o || !provider || !account_id)
        return OAUTH2_ERROR_INVALID;

    /* If the provider has a revoke_token callback, retrieve the access token
     * and call it to invalidate server-side before deleting locally. */
    const OAuth2ProviderOps *ops = find_provider(o, provider);
    if (ops && ops->revoke_token) {
        BindText2 select_params = { provider, account_id };
        TokenRow *row = sqlite_dal_query_one_p(o->dal,
            "SELECT access_token, refresh_token, expires_at FROM oauth2_tokens "
            "WHERE provider = ? AND account_id = ?",
            bind_text2, &select_params, map_token_row, NULL);

        if (row) {
            char plain_at[ENCRYPTED_TOKEN_MAX] = {0};
            if (o->encryption_enabled) {
                decrypt_token(o, row->access_token, plain_at, sizeof(plain_at));
            } else {
                memcpy(plain_at, row->access_token, sizeof(plain_at));
            }
            free(row);

            if (plain_at[0] != '\0') {
                OAuth2Error rev_err = ops->revoke_token(NULL, plain_at);
                if (rev_err != OAUTH2_OK) {
                    fprintf(stderr, "Warning: server-side token revocation failed "
                            "for provider '%s': %s\n", provider,
                            oauth2_error_string(rev_err));
                }
            }
            mbedtls_platform_zeroize(plain_at, sizeof(plain_at));
        }
    }

    BindText2 params = { provider, account_id };
    int rc = sqlite_dal_exec_p(o->dal,
        "DELETE FROM oauth2_tokens WHERE provider = ? AND account_id = ?",
        bind_text2, &params);

    return (rc >= 0) ? OAUTH2_OK : OAUTH2_ERROR_STORAGE;
}

/* Exposed for testing — forces expiry of all pending auth states */
void oauth2_store_expire_pending(oauth2_store_t *o) {
    if (!o) return;
    expire_pending_auths(o);
}
